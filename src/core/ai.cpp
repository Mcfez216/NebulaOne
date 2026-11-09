// NebulaOne - AI corporations. They see the same markets and give the same
// fleet orders the player does; nothing here touches state the player cannot.
//
// On large charts the AI never scans the whole galaxy: each decision samples
// stations and systems within a few jumps of where the fleet is.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

static bool fleetIdle(const Fleet& f) {
    if (!f.alive || f.battle != NONE || f.act != Act::Idle) return false;
    if (f.order.kind == OrderKind::None || f.phase == 99) return true;
    if (f.order.kind == OrderKind::Move && f.docked == f.order.station) return true;
    return false;
}

static int roleCount(const World& w, const Corp& c, ShipRole role) {
    int n = 0;
    for (Id s : c.shipList)
        if (w.ships[s].alive && w.ships[s].corp == c.id && shipDef(w.ships[s].def).role == role) n++;
    return n;
}

static bool zoneOk(const World& w, Id sys, bool timid) {
    const StarSystem& s = w.systems[sys];
    if (s.zone == Zone::Frontier) return false;
    if (timid && s.zone != Zone::Charter) return false;
    return true;
}

template <class V> static void sampleDown(World& w, V& v, size_t n) {
    for (size_t i = 0; i < v.size() && i < n; i++) std::swap(v[i], v[i + (size_t)w.rng.irange(0, (int)(v.size() - i - 1))]);
    if (v.size() > n) v.resize(n);
}

void World::aiFormFleets(Corp& c) {
    std::map<std::pair<Id, int>, std::vector<Id>> groups;
    int haulerFleets = 0;
    for (Id fid : c.fleetList) {
        const Fleet& f = fleets[fid];
        if (f.alive && f.corp == c.id && !f.ships.empty() && shipDef(ships[f.ships[0]].def).role == ShipRole::Hauler) haulerFleets++;
    }
    for (Id sid : c.shipList) {
        const Ship& s = ships[sid];
        if (!s.alive || s.corp != c.id || s.fleet != NONE || s.station == NONE) continue;
        if (!stationUsable(s.station)) continue;
        ShipRole r = shipDef(s.def).role;
        if (c.arch == Arch::Industrialist) {
            if (r != ShipRole::Hauler || haulerFleets >= 2) continue;
            haulerFleets++;
        }
        if ((c.arch == Arch::Pirate || c.arch == Arch::Ganker || c.arch == Arch::Mercenary) && r != ShipRole::Combat) continue;
        if (c.arch == Arch::Trader && r != ShipRole::Hauler) continue;
        groups[{s.station, (int)r}].push_back(s.id);
    }
    static const char* roleWord[] = {"Mining", "Freight", "Strike", "Survey"};
    for (auto& [key, ids] : groups) {
        size_t per = key.second == (int)ShipRole::Combat ? 6 : key.second == (int)ShipRole::Mining ? 3 : 1;
        for (size_t i = 0; i < ids.size(); i += per) {
            std::vector<Id> part(ids.begin() + (long)i, ids.begin() + (long)std::min(ids.size(), i + per));
            createFleet(c.id, part, c.ticker + " " + roleWord[key.second] + " " + std::to_string(c.fleetList.size() + 1));
        }
    }
}

void World::aiSellHangar(Corp& c, Id station, bool mineralsToo) {
    if (!stationUsable(station)) return;
    std::vector<std::pair<int, int64_t>> stock;
    for (auto it = c.hangar.lower_bound(hkey(station, 0)); it != c.hangar.end() && it->first < hkey(station + 1, 0); ++it)
        if (it->second > 0) stock.push_back({(int)(it->first % HANGAR_KEY), it->second});
    for (auto& [it, q] : stock) {
        if (isBpo(it)) continue;
        if (it == FUEL_CELLS && stations[station].isOutpost() && stations[station].owner == c.id) continue;
        if (isOre(it)) {
            if (q >= 100) startRefine(c.id, station, it, q);
            continue;
        }
        if (isMineral(it) && !mineralsToo) continue;
        if (c.arch == Arch::Industrialist && (isMineral(it) || (isComponent(it) && q < 3000) || isStructure(it))) continue;
        Money ref = refPrice(station, it);
        std::vector<int64_t> stale;
        if (const Book* bk = stations[station].findBook(it))
            for (auto& o : bk->sells)
                if (o.corp == c.id && (o.price > ref * 102 / 100 || now - o.placed > 2 * MINUTES_PER_DAY)) stale.push_back(o.id);
        for (auto id : stale) cancelOrder(id, c.id);
        int64_t q2 = have(c.id, station, it);
        instantSell(c.id, station, it, q2, ref * 88 / 100);
        int64_t left = have(c.id, station, it);
        if (left <= 0) continue;
        bool listed = false;
        if (const Book* bk = stations[station].findBook(it))
            for (auto& o : bk->sells) listed = listed || o.corp == c.id;
        if (!listed) {
            Money ask = bestAsk(station, it, nullptr, c.id);
            Money px = std::max(ref * 93 / 100, ask ? ask * 99 / 100 : ref);
            placeOrder(c.id, station, it, false, std::min(px, ref * 97 / 100), left);
        }
    }
}

static Money unsoldMinerals(const World& w, const Corp& c) {
    Money v = 0;
    for (auto& [k, q] : c.hangar) {
        int it = (int)(k % HANGAR_KEY);
        if (isMineral(it) || isOre(it)) v += item(it).base * q;
    }
    for (int64_t spot : c.orderSpots) {
        int it = (int)(spot % HANGAR_KEY);
        if (!(isMineral(it) || isOre(it))) continue;
        if (const Book* bk = w.stations[(Id)(spot / HANGAR_KEY)].findBook(it))
            for (auto& o : bk->sells)
                if (o.corp == c.id) v += item(it).base * o.qty;
    }
    return v;
}

void World::aiBuyShips(Corp& c) {
    if (now < c.cooldownUntil) return;
    int want[3] = {0, 0, 0};  // mining, hauler, combat
    double nwM = toCr(c.netWorth) / 1e6;
    bool escorted = roleCount(*this, c, ShipRole::Combat) > 0 && c.arch == Arch::Miner;
    switch (c.arch) {
        case Arch::Trader: want[1] = (int)std::clamp(3 + nwM / 80, 3.0, 6.0); break;
        case Arch::Miner: want[0] = (int)std::clamp(6 + nwM / 25, 6.0, 14.0); want[2] = escorted ? 3 : 0; break;
        case Arch::Industrialist: want[1] = 2; break;
        case Arch::Pirate: want[2] = (int)std::clamp(5 + nwM / 30, 5.0, 10.0); break;
        case Arch::Ganker: want[2] = 5; break;
        case Arch::Mercenary: want[2] = (int)std::clamp(6 + nwM / 30, 6.0, 9.0); break;
        default: return;
    }
    const int choice[3][2] = {{QUARRIER, MATTOCK}, {DROVER, CARRACK}, {LANCER, WARDEN}};
    Id homeSys = stations[c.home].system;
    std::vector<int> nearD;
    std::vector<Id> near = systemsWithin(homeSys, 6, &nearD);
    for (int r = 0; r < 3; r++) {
        int have_ = roleCount(*this, c, (ShipRole)r);
        if (have_ >= want[r]) continue;
        int def = choice[r][0];
        if (r == 1 && nwM > 150 && rng.chance(0.4)) def = CARRACK;
        if (r == 2 && nwM > 60 && rng.chance(0.35)) def = WARDEN;
        if (r == 0 && nwM < 20) def = MATTOCK;
        Money px = item(SHIP_FIRST + def).base;
        bool outlaw = c.arch == Arch::Pirate || c.arch == Arch::Ganker;
        if (c.wallet < px * (outlaw ? 6 : 15) / 5) continue;
        Id best = NONE;
        int bestD = 99;
        for (size_t k = 0; k < near.size() && best == NONE; k++)
            for (Id st : systems[near[k]].stations) {
                if (!stationUsable(st)) continue;
                int64_t depth = 0;
                Money ask = bestAsk(st, SHIP_FIRST + def, &depth, c.id);
                if (!ask || ask > px * 13 / 10) continue;
                if (nearD[k] < bestD) { bestD = nearD[k]; best = st; }
            }
        if (best == NONE) continue;
        int64_t got = instantBuy(c.id, best, SHIP_FIRST + def, 1, px * 13 / 10);
        if (got > 0 && c.arch == Arch::Ganker) c.cooldownUntil = now + 4 * 60;
    }
}

void World::aiTrader(Corp& c, Fleet& f) {
    Id loc = f.docked != NONE ? stations[f.docked].system : f.system;
    double cap = fleetCargoCap(f);
    Money budget = std::max<Money>(0, c.wallet * 4 / 10);
    if (cap <= 0 || budget < 100000 * CR) return;
    bool timid = (c.id % 3) != 0 || c.shipsLost > 25;  // some traders run Contract space until it costs them too much
    std::vector<Id> srcs, dsts;
    std::vector<int> srcD;
    std::vector<Id> srcSys = systemsWithin(loc, 3, &srcD);
    for (Id sy : srcSys)
        if (zoneOk(*this, sy, timid))
            for (Id st : systems[sy].stations)
                if (stationUsable(st)) srcs.push_back(st);
    for (Id sy : systemsWithin(loc, 9))
        if (zoneOk(*this, sy, timid))
            for (Id st : systems[sy].stations)
                if (stationUsable(st)) dsts.push_back(st);
    sampleDown(*this, srcs, 12);
    sampleDown(*this, dsts, 40);
    if (srcs.empty() || dsts.size() < 2) return;
    int itFirst = MINERAL_FIRST, itLast = c.arch == Arch::Industrialist ? GOOD_FIRST : SHIP_FIRST;
    int nI = itLast - itFirst;
    std::vector<Money> bid(dsts.size() * nI, 0);
    std::vector<int64_t> bidDepth(dsts.size() * nI, 0);
    for (size_t b = 0; b < dsts.size(); b++)
        for (int it = itFirst; it < itLast; it++) bid[b * nI + (it - itFirst)] = bestBid(dsts[b], it, &bidDepth[b * nI + (it - itFirst)], c.id);
    double mph = fleetMinutesPerHex(f);
    double bestScore = 0;
    Id bA = NONE, bB = NONE;
    int bItem = -1;
    int64_t bQty = 0;
    Money bAsk = 0, bBid = 0;
    for (Id A : srcs) {
        Id sa = stations[A].system;
        int dA = hexDistance(systems[loc].hex, systems[sa].hex);
        for (int it = itFirst; it < itLast; it++) {
            int64_t depthA = 0;
            Money ask = bestAsk(A, it, &depthA, c.id);
            if (!ask || depthA <= 0) continue;
            double vol = item(it).volume;
            for (size_t b = 0; b < dsts.size(); b++) {
                Id B = dsts[b];
                if (B == A) continue;
                Money bb = bid[b * nI + (it - itFirst)];
                if (bb <= ask * 106 / 100) continue;
                int64_t q = std::min({depthA, bidDepth[b * nI + (it - itFirst)], (int64_t)(cap / vol), (int64_t)(budget / ask)});
                if (q <= 0) continue;
                double tax = salesTax(c.id, B);
                double profit = q * ((double)bb * (1.0 - tax) - (double)ask);
                // straight-line hexes, stretched a little for the lanes' detours
                int dAB = hexDistance(systems[sa].hex, systems[stations[B].system].hex);
                double minutes = (dA + dAB) * 1.35 * mph + 45.0 + (systems[stations[B].system].zone != Zone::Charter ? 60.0 : 0.0);
                double score = profit / minutes;
                if (score > bestScore) { bestScore = score; bA = A; bB = B; bItem = it; bQty = q; bAsk = ask; bBid = bb; }
            }
        }
    }
    if (bA == NONE || bestScore * 60 < 20000.0 * CR) {
        if (f.docked == NONE) {
            FleetOrder o;
            o.kind = OrderKind::Move;
            o.station = c.home;
            setOrder(f, o);
        }
        return;
    }
    FleetOrder o;
    o.kind = OrderKind::Trade;
    o.station = bA;
    o.station2 = bB;
    o.item = bItem;
    o.qty = bQty;
    o.price = bAsk * 103 / 100;
    o.price2 = bBid * 94 / 100;
    o.policy = timid ? RoutePolicy::LowestRisk : RoutePolicy::AvoidFrontier;
    setOrder(f, o);
}

void World::aiMiner(Corp& c, Fleet& f) {
    bool combat = false;
    for (Id s : f.ships) combat = combat || shipDef(ships[s].def).role == ShipRole::Combat;
    Id homeSys = stations[c.home].system;
    if (combat) {
        Id target = homeSys;
        for (Id gid : c.fleetList) {
            const Fleet& g = fleets[gid];
            if (g.alive && g.order.kind == OrderKind::Mine && g.order.system != NONE) { target = g.order.system; break; }
        }
        FleetOrder o;
        o.kind = OrderKind::Patrol;
        o.system = target;
        setOrder(f, o);
        return;
    }
    bool deep = systems[homeSys].zone == Zone::Contract;
    double bestScore = 0;
    Id bSys = NONE;
    int bBelt = -1;
    std::vector<int> nd;
    std::vector<Id> near = systemsWithin(homeSys, 6, &nd);
    for (size_t k = 0; k < near.size(); k++) {
        Id sy = near[k];
        const StarSystem& s = systems[sy];
        if (s.zone == Zone::Frontier || (!deep && s.zone != Zone::Charter)) continue;
        if (s.heat > 3.0f) continue;
        int d = nd[k];
        int crowd = 0;
        for (Id gid : c.fleetList) crowd += fleets[gid].alive && fleets[gid].order.kind == OrderKind::Mine && fleets[gid].order.system == sy;
        for (int b = 0; b < (int)s.belts.size(); b++) {
            const Belt& bl = s.belts[b];
            if (bl.remaining < bl.capacity * 0.2 || bl.capacity <= 0 || !beltVisible(bl, c.id)) continue;
            double perfect = 0;
            for (int m = 0; m < MINERAL_COUNT; m++) perfect += item(bl.ore).refine[m] / 100.0 * (double)refPrice(c.home, MINERAL_FIRST + m);
            double score = perfect / (1.0 + 0.25 * d) / (1.0 + crowd) / (1.0 + s.risk / 60.0);
            if (score > bestScore) { bestScore = score; bSys = sy; bBelt = b; }
        }
    }
    if (bSys == NONE) return;
    Id unload = NONE;
    int bd = 99;
    std::vector<int> ud;
    std::vector<Id> unear = systemsWithin(bSys, 4, &ud);
    for (size_t k = 0; k < unear.size(); k++)
        for (Id st : systems[unear[k]].stations) {
            if (!stationUsable(st)) continue;
            int d = ud[k];
            if (d < bd || (d == bd && stations[st].type == StationType::MiningColony)) { bd = d; unload = st; }
        }
    if (unload == NONE) unload = c.home;
    FleetOrder o;
    o.kind = OrderKind::Mine;
    o.system = bSys;
    o.belt = bBelt;
    o.station = unload;
    o.policy = RoutePolicy::LowestRisk;
    setOrder(f, o);
}

void World::aiIndustry(Corp& c) {
    Id H = c.home;
    if (!stationUsable(H)) return;
    // sell finished hulls, modules and copies a little over cost
    for (int it = MODULE_FIRST; it < STRUCTURE_FIRST; it++) {
        int64_t n = have(c.id, H, it);
        if (n <= 0) continue;
        bool listed = false;
        if (const Book* bk = stations[H].findBook(it))
            for (auto& o : bk->sells) listed = listed || o.corp == c.id;
        if (listed) continue;
        const BlueprintDef& d = blueprint(item(it).bp);
        Money cost = 0;
        for (auto& [m, q] : d.materials) cost += refPrice(H, m) * matNeed(q, 1, c.me[item(it).bp]);
        Money ask = bestAsk(H, it, nullptr, c.id);
        Money px = std::max(cost * 108 / 100, ask ? ask * 98 / 100 : item(it).base);
        placeOrder(c.id, H, it, false, px, n);
    }
    // the most profitable job we hold a free blueprint for
    int bestBp = -1;
    double bestMargin = 1.06;
    for (int bp = 0; bp < BP_OUTPOST; bp++) {
        const BlueprintDef& d = blueprint(bp);
        if (have(c.id, H, d.bpo) <= 0) continue;
        bool busy = false;
        for (auto& j : jobs) busy = busy || (j.active && j.kind == JobKind::Manufacture && j.corp == c.id && j.item == bp);
        if (busy) continue;
        int64_t listedQty = have(c.id, H, d.product);
        if (const Book* bk = stations[H].findBook(d.product))
            for (auto& o : bk->sells) listedQty += (o.corp == c.id) ? o.qty : 0;
        if (listedQty >= (isComponent(d.product) ? 4000 : 3)) continue;
        double cost = 0;
        for (auto& [m, q] : d.materials) {
            Money ask = bestAsk(H, m, nullptr, c.id);
            cost += (double)(ask ? ask : refPrice(H, m) * 11 / 10) * matNeed(q, 1, c.me[bp]);
        }
        double sale = (double)refPrice(H, d.product) * d.perRun;
        if (cost > 0 && sale / cost > bestMargin) { bestMargin = sale / cost; bestBp = bp; }
    }
    if (bestBp < 0) return;
    const BlueprintDef& d = blueprint(bestBp);
    int64_t runs = c.wallet > d.materialBase() * 6 ? 2 : 1;
    bool ready = true;
    for (auto& [m, q] : d.materials) {
        int64_t need = matNeed(q, runs, c.me[bestBp]) - have(c.id, H, m);
        if (need <= 0) continue;
        instantBuy(c.id, H, m, need, refPrice(H, m) * 13 / 10);
        need = matNeed(q, runs, c.me[bestBp]) - have(c.id, H, m);
        if (need > 0) {
            ready = false;
            bool bidding = false;
            if (const Book* bk = stations[H].findBook(m))
                for (auto& o : bk->buys) bidding = bidding || o.corp == c.id;
            if (!bidding) placeOrder(c.id, H, m, true, item(m).base * 105 / 100, need);
        }
    }
    if (ready) startManufacture(c.id, H, bestBp, runs);
}

void World::aiPirate(Corp& c, Fleet& f) {
    bool ganker = c.arch == Arch::Ganker;
    double hullFrac = 0, n = 0;
    for (Id s : f.ships) { hullFrac += ships[s].hull / stats(ships[s]).hull; n++; }
    hullFrac = n > 0 ? hullFrac / n : 1;
    Money loot = fleetCargoValue(f);
    if (f.docked == c.home) {
        unloadAll(f, c.home);
        aiSellHangar(c, c.home, true);
    } else if ((loot > 1500000 * CR || hullFrac < 0.55) && f.order.kind != OrderKind::Move) {
        FleetOrder o;
        o.kind = OrderKind::Move;
        o.station = c.home;
        setOrder(f, o);
        return;
    }
    double threat = 0;
    for (Id gid : fleetsIn[f.system]) {
        const Fleet& g = fleets[gid];
        if (g.alive && g.docked == NONE && g.order.kind == OrderKind::Patrol && hostileTo(g.corp, c.id)) threat += fleetStrength(g);
    }
    double mine = fleetStrength(f);
    bool relocate = (f.order.kind != OrderKind::Raid && f.order.kind != OrderKind::Siege) || f.phase == 99 || f.docked != NONE ||
                    rng.chance(0.08) || threat > 0.8 * mine;
    if (!relocate) return;
    Id homeSys = stations[c.home].system;
    // strong pirate bands sometimes go after outposts nearby
    if (!ganker && mine > 120 && rng.chance(0.06)) {
        for (Id sid : outpostIds) {
            const Station& o = stations[sid];
            if (o.dead || o.owner == c.id || o.state == OutpostState::Anchoring || hexDistance(systems[homeSys].hex, systems[o.system].hex) > 14) continue;
            if (standing(c.id, o.owner) >= 0) continue;
            FleetOrder so;
            so.kind = OrderKind::Siege;
            so.system = o.system;
            so.station = sid;
            setOrder(f, so);
            return;
        }
    }
    double best = -1;
    Id target = NONE;
    for (Id sy : systemsWithin(homeSys, ganker ? 4 : 5)) {
        const StarSystem& s = systems[sy];
        if (ganker ? !(s.zone == Zone::Charter || (s.zone == Zone::Contract && s.depth == 1)) : s.zone == Zone::Charter) continue;
        double traffic = s.stations.size() * 1.5 + s.belts.size() * 0.6 + s.lanes.size() * 0.5;
        double danger = 0;
        for (Id gid : fleetsIn[sy]) {
            const Fleet& g = fleets[gid];
            if (!g.alive || g.corp == c.id) continue;
            traffic += 1.0;
            if (g.order.kind == OrderKind::Patrol && hostileTo(g.corp, c.id)) danger += fleetStrength(g);
        }
        double score = traffic * rng.range(0.6, 1.4) - danger * 0.8;
        for (auto& e : events)  // raider surges draw the bands in
            if (e.applied && !e.over && e.kind == EventKind::PirateSurge && e.region == s.region) score *= 1.8;
        if (score > best) { best = score; target = sy; }
    }
    if (target == NONE) return;
    FleetOrder o;
    o.kind = OrderKind::Raid;
    o.system = target;
    o.policy = RoutePolicy::Shortest;
    setOrder(f, o);
}

void World::aiMercenary(Corp& c, Fleet& f) {
    double hullFrac = 0, n = 0;
    for (Id s : f.ships) { hullFrac += ships[s].hull / stats(ships[s]).hull; n++; }
    if (n > 0 && hullFrac / n < 0.5 && f.order.kind != OrderKind::Move) {
        FleetOrder o;
        o.kind = OrderKind::Move;
        o.station = c.home;
        setOrder(f, o);
        return;
    }
    bool relocate = f.order.kind != OrderKind::Patrol || f.docked != NONE || rng.chance(0.07);
    if (!relocate) return;
    Id homeSys = stations[c.home].system;
    double best = -1;
    Id target = NONE;
    for (Id sy : systemsWithin(homeSys, 6)) {
        const StarSystem& s = systems[sy];
        if (s.zone == Zone::Charter && s.depth == 0) continue;
        double outlaws = 0;
        for (Id gid : fleetsIn[sy])
            if (fleets[gid].alive && isOutlaw(fleets[gid].corp)) outlaws += fleetStrength(fleets[gid]);
        double score = s.heat * 2.0 + outlaws * 0.3 + s.risk * 0.02 + rng.range(0, 2.0);
        if (score > best) { best = score; target = sy; }
    }
    if (target == NONE) return;
    FleetOrder o;
    o.kind = OrderKind::Patrol;
    o.system = target;
    setOrder(f, o);
}

void World::aiContracts(Corp& c) {
    if (c.arch == Arch::Mercenary) {
        for (auto& ct : contracts)
            if (ct.state == ContractState::Open && ct.kind == ContractKind::Bounty && ct.expires > now && hostileTo(c.id, ct.target) &&
                rng.chance(0.3)) {
                acceptContract(c.id, ct.id);
                return;
            }
        return;
    }
    if (c.arch != Arch::Trader) return;
    for (Id fid : c.fleetList) {
        Fleet& f = fleets[fid];
        if (!fleetIdle(f) || f.docked == NONE) continue;
        double cap = fleetCargoCap(f);
        for (auto& ct : contracts) {
            if (ct.state != ContractState::Open || ct.kind != ContractKind::Courier || ct.expires <= now) continue;
            if (ct.qty * item(ct.item).volume > cap || c.wallet < ct.collateral * 3) continue;
            int d = (hexDistance(systems[stations[f.docked].system].hex, systems[stations[ct.from].system].hex) +
                     hexDistance(systems[stations[ct.from].system].hex, systems[stations[ct.to].system].hex)) / 2;
            if (d > 12 || ct.reward < (Money)d * 60000 * CR) continue;
            if (!zoneOk(*this, stations[ct.to].system, false)) continue;
            if (!acceptContract(c.id, ct.id).ok) continue;
            FleetOrder o;
            o.kind = OrderKind::Haul;
            o.station = ct.from;
            o.station2 = ct.to;
            o.item = ct.item;
            o.qty = ct.qty;
            o.contract = ct.id;
            o.policy = RoutePolicy::LowestRisk;
            setOrder(f, o);
            return;
        }
        break;
    }
}

static void aiDefendOutposts(World& w, Corp& c) {
    for (Id sid : w.outpostIds) {
        const Station& o = w.stations[sid];
        if (o.dead || o.owner != c.id || o.besieger == NONE || w.now - o.lastSiege > 3 * MINUTES_PER_HOUR) continue;
        for (Id fid : c.fleetList) {
            Fleet& f = w.fleets[fid];
            if (!f.alive || f.battle != NONE || w.fleetStrength(f) <= 0) continue;
            if (f.order.kind == OrderKind::Patrol && f.order.system == o.system) continue;
            FleetOrder p;
            p.kind = OrderKind::Patrol;
            p.system = o.system;
            w.setOrder(f, p);
        }
        return;
    }
}

void World::aiThink(Corp& c) {
    aiFormFleets(c);
    if (rng.chance(0.25)) aiBuyShips(c);
    if (c.wallet < 3000000 * CR && c.rating < 4) {
        Money lim = bankCreditLimit(c.id);
        if (lim > 5000000 * CR && rng.chance(0.2)) takeBankLoan(c.id, 5000000 * CR, 30);
    }
    if (rng.chance(0.10)) aiContracts(c);
    aiDefendOutposts(*this, c);
    if (rng.chance(0.3)) aiWar(c);
    for (Id fid : std::vector<Id>(c.fleetList)) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.corp != c.id || f.battle != NONE) continue;
        if (f.order.targetCorp != NONE && (f.order.kind == OrderKind::Patrol || f.order.kind == OrderKind::Siege)) {
            if (atWar(c.id, f.order.targetCorp) || findTreaty(Pact::War, c.id, f.order.targetCorp, false)) continue;  // fighting a war
            setOrder(f, FleetOrder{});
        }
        switch (c.arch) {
            case Arch::Trader:
            case Arch::Industrialist:
                if (fleetIdle(f)) {
                    if (f.docked != NONE) aiSellHangar(c, f.docked, true);
                    aiTrader(c, f);
                }
                break;
            case Arch::Miner: {
                bool glut = unsoldMinerals(*this, c) > 60000000 * CR;
                if (glut && f.order.kind == OrderKind::Mine && rng.chance(0.3)) {
                    FleetOrder o;
                    o.kind = OrderKind::Move;
                    o.station = f.order.station;
                    setOrder(f, o);
                } else if (fleetIdle(f) && !glut) {
                    aiMiner(c, f);
                }
                break;
            }
            case Arch::Pirate:
            case Arch::Ganker:
                if (f.act == Act::Idle || f.act == Act::Wait) aiPirate(c, f);
                break;
            case Arch::Mercenary:
                if (f.act == Act::Idle || f.act == Act::Wait) aiMercenary(c, f);
                break;
            default: break;
        }
    }
    if (c.arch == Arch::Miner || rng.chance(0.15)) {
        std::vector<Id> sts;
        for (auto& [k, q] : c.hangar) {
            Id st = (Id)(k / HANGAR_KEY);
            if (sts.empty() || sts.back() != st) sts.push_back(st);
        }
        for (Id st : sts) aiSellHangar(c, st, c.arch != Arch::Industrialist);
    }
    if (c.arch == Arch::Industrialist && rng.chance(0.3)) aiIndustry(c);
}

}  // namespace nb
