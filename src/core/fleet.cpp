// NebulaOne - fleets, standing orders, mining, hauling, encounters and combat.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

static bool needsPilots(const World& w, const Fleet& f) {
    if (!w.corps[f.corp].player) return false;
    for (Id s : f.ships) {
        const Ship& sh = w.ships[s];
        if (sh.pilot == NONE) return true;
        if (w.people[sh.pilot].injuredUntil > w.now) return true;
    }
    return false;
}

Id World::createFleet(Id corp, const std::vector<Id>& shipIds, const std::string& name) {
    if (shipIds.empty()) return NONE;
    Fleet f;
    f.id = (Id)fleets.size();
    f.corp = corp;
    f.name = name;
    Id st = ships[shipIds[0]].station;
    f.docked = st;
    f.system = stations[st].system;
    for (Id s : shipIds) {
        Ship& sh = ships[s];
        if (!sh.alive || sh.corp != corp || sh.fleet != NONE || sh.station != st) continue;
        sh.fleet = f.id;
        f.ships.push_back(s);
    }
    if (f.ships.empty()) return NONE;
    f.status = "Docked";
    fleets.push_back(f);
    corps[corp].fleetList.push_back(f.id);
    return f.id;
}

void World::setOrder(Fleet& f, const FleetOrder& o) {
    f.order = o;
    f.phase = 0;
    f.path.clear();
    f.spent = f.earned = 0;
    if (f.act == Act::Wait) f.actEnd = now;  // stop waiting
    f.status = std::string("New order: ") + orderKindName(o.kind);
}

void World::beginAct(Fleet& f, Act a, Tick minutes) {
    f.act = a;
    f.actStart = now;
    f.actEnd = now + std::max<Tick>(1, minutes);
}

double World::routeRisk(const std::vector<Id>& path, Id from) const {
    double r = from != NONE ? systems[from].risk : 0;
    for (Id s : path) r = std::max(r, (double)systems[s].risk);
    return r;
}

// ------------------------------------------------------------ movement -----
static bool linked(const World& w, Id a, Id b, Id corp) {
    for (auto& l : w.systems[a].lanes)
        if (l.to == b) return true;
    for (auto& e : w.drift)
        if (e.corp == corp && ((e.a == a && e.b == b) || (e.a == b && e.b == a))) return true;
    return false;
}

static bool startJump(World& w, Fleet& f, Id targetSys) {
    if (f.path.empty() || f.path.back() != targetSys || f.path.front() == f.system || !linked(w, f.system, f.path.front(), f.corp)) {
        f.path = w.route(f.system, targetSys, f.order.policy, f.corp);
        if (f.path.empty()) { f.status = "No route"; return false; }
    }
    if (w.stormBlocked(f.path.front()) || w.stormBlocked(f.system)) {
        // lanes into and out of a storm are closed: wait it out
        w.beginAct(f, Act::Wait, 30);
        f.status = "Waiting out the ion storm over " + w.systems[w.stormBlocked(f.system) ? f.system : f.path.front()].name;
        return false;
    }
    f.jumpTo = f.path.front();
    w.payToll(f, f.system, f.jumpTo);
    int extra = f.atBelt ? 3 : 0;
    f.atBelt = false;
    w.beginAct(f, Act::Jump, w.laneMinutes(f, f.system, f.jumpTo) + extra);
    f.status = "En route to " + w.systems[targetSys].name;
    return true;
}

bool World::goTo(Fleet& f, Id station) {
    if (f.docked == station) return true;
    if (f.docked != NONE) {
        beginAct(f, Act::Undock, 2);
        f.status = "Undocking";
        return false;
    }
    if (!stationUsable(station)) { f.status = "Destination is not accepting ships"; f.phase = 99; return false; }
    Id sys = stations[station].system;
    if (f.system != sys) { startJump(*this, f, sys); return false; }
    f.jumpTo = station;
    beginAct(f, Act::Dock, f.atBelt ? 5 : 2);
    f.atBelt = false;
    f.status = "Docking at " + stations[station].name;
    return false;
}

bool World::goToSystem(Fleet& f, Id sys) {
    if (f.docked != NONE) {
        beginAct(f, Act::Undock, 2);
        f.status = "Undocking";
        return false;
    }
    if (f.system != sys) { startJump(*this, f, sys); return false; }
    return true;
}

void World::unloadAll(Fleet& f, Id station, bool oreOnly) {
    for (auto it = f.cargo.begin(); it != f.cargo.end();) {
        if (oreOnly && !isOre(it->first)) { ++it; continue; }
        addItem(f.corp, station, it->first, it->second);
        it = f.cargo.erase(it);
    }
    if (f.cargo.empty()) f.cargoCover = 0;
}

void World::repairFleet(Fleet& f) {
    Money cost = 0;
    for (Id s : f.ships) {
        Ship& sh = ships[s];
        double mx = stats(sh).hull;
        if (sh.hull < mx) {
            cost += (Money)((1.0 - sh.hull / mx) * shipValue(sh) * 0.08);
            sh.hull = mx;
        }
    }
    if (cost > 0) pay(f.corp, -cost, Cat::Repairs, "Repairs for " + f.name);
}

// Insure what is in the hold for the trip ahead, if the order asks for it.
static void insureCargo(World& w, Fleet& f, Id toStation) {
    if (!f.order.insureCargo || f.cargo.empty()) return;
    Money value = w.fleetCargoValue(f);
    auto path = w.route(f.system, w.stations[toStation].system, f.order.policy);
    double risk = w.routeRisk(path, f.system);
    Money prem = w.cargoPremium(value, risk);
    if (w.corps[f.corp].wallet < prem) return;
    w.pay(f.corp, -prem, Cat::Insurance, "Cargo cover for " + f.name + " (" + riskGrade((float)risk) + " route)");
    f.cargoCover = value;
}

void World::payCargoCover(Fleet& f, Money lostValue) {
    if (f.cargoCover <= 0 || lostValue <= 0) return;
    Money claim = std::min(f.cargoCover, lostValue) * 8 / 10;
    f.cargoCover = std::max<Money>(0, f.cargoCover - lostValue);
    pay(f.corp, claim, Cat::InsurancePayout, "Cargo claim: " + f.name);
    if (corps[f.corp].player) addLog(Chan::Corp, Sev::Info, true, f.system, "Underwriters paid " + fmtMoney(claim) + " for cargo lost from " + f.name + ".");
}

// Completion of the current activity.
void World::fleetArrive(Fleet& f) {
    switch (f.act) {
        case Act::Undock: f.docked = NONE; break;
        case Act::Dock:
            if (!stationUsable(f.jumpTo)) { f.jumpTo = NONE; break; }
            f.docked = f.jumpTo;
            f.jumpTo = NONE;
            f.status = "Docked";
            markActive(f.system);
            repairFleet(f);
            if (fog && corps[f.corp].player) snapshotStation(f.docked);
            break;
        case Act::Jump:
            f.system = f.jumpTo;
            f.jumpTo = NONE;
            if (!f.path.empty() && f.path.front() == f.system) f.path.erase(f.path.begin());
            for (Id s : f.ships) ships[s].trips++;
            if (!systems[f.system].stations.empty()) markActive(f.system);
            break;
        case Act::ToBelt: f.atBelt = true; break;
        case Act::Survey: f.act = Act::Idle; surveyComplete(f); return;
        case Act::Salvage: f.act = Act::Idle; salvageComplete(f); return;
        case Act::Mine: {
            StarSystem& s = systems[f.system];
            if (f.order.belt < 0 || f.order.belt >= (int)s.belts.size()) break;
            Belt& b = s.belts[f.order.belt];
            double mins = (double)(f.actEnd - f.actStart);
            double rate = 0;
            for (Id id : f.ships) {
                const Ship& sh = ships[id];
                rate += stats(sh).mining * pilotFactor(sh, 0) * (1.0 + 0.1 * execBonus(f.corp, SEAT_MINING));
            }
            double amount = rate * mins / 60.0;
            amount = std::min({amount, b.remaining, fleetFreeFor(f, b.ore) / item(b.ore).volume});
            int64_t n = (int64_t)amount;
            if (n > 0) {
                f.cargo[b.ore] += n;
                b.remaining -= (double)n;
                double share = rate > 0 ? n / rate : 0;
                for (Id id : f.ships) ships[id].mined += stats(ships[id]).mining * pilotFactor(ships[id], 0) * share;
                if (f.corp == player && s.zone != Zone::Charter) pstats.riskyMinedUnits += n;
            }
            break;
        }
        default: break;
    }
    f.act = Act::Idle;
}

void World::fleetTick(Fleet& f) {
    if (f.battle != NONE) return;
    if (f.act != Act::Idle) {
        if (now < f.actEnd) return;
        fleetArrive(f);
    }
    fleetPlan(f);
}

// Decide the next activity from the standing order.
void World::fleetPlan(Fleet& f) {
    if (f.ships.empty()) { f.alive = false; return; }
    FleetOrder& o = f.order;
    if (o.kind == OrderKind::None || f.phase == 99) {
        if (f.docked == NONE && o.kind == OrderKind::None) {
            Id st = nearestStation(f.system);
            if (st != NONE) { goTo(f, st); return; }
        }
        if (f.docked != NONE && f.phase != 99) f.status = "Docked";
        return;
    }
    if (needsPilots(*this, f)) {
        f.status = "Needs pilots (People window)";
        return;
    }
    switch (o.kind) {
        case OrderKind::Move:
            if (goTo(f, o.station)) f.status = "Docked at destination";
            break;

        case OrderKind::Mine: {
            StarSystem& s = systems[o.system];
            if (o.belt < 0 || o.belt >= (int)s.belts.size() || !beltVisible(s.belts[o.belt], f.corp)) { f.status = "Invalid belt"; f.phase = 99; return; }
            if (f.phase == 0) {
                Belt& b = s.belts[o.belt];
                double free = fleetFreeFor(f, b.ore);
                if (free < 1.0 || (f.atBelt && b.remaining < 1.0)) {
                    if (b.remaining < 1.0) {
                        int best = -1;
                        for (int k = 0; k < (int)s.belts.size(); k++)
                            if (s.belts[k].remaining > 1000 && beltVisible(s.belts[k], f.corp) &&
                                (best < 0 || s.belts[k].remaining > s.belts[best].remaining))
                                best = k;
                        if (best >= 0 && free >= 1.0) { o.belt = best; f.atBelt = false; return; }
                    }
                    f.phase = 1;
                    fleetPlan(f);
                    return;
                }
                if (!goToSystem(f, o.system)) return;
                if (!f.atBelt) {
                    beginAct(f, Act::ToBelt, 3);
                    f.status = "Moving to belt";
                    return;
                }
                beginAct(f, Act::Mine, 10);
                f.status = "Mining " + item(b.ore).name;
            } else {
                if (!goTo(f, o.station)) return;
                int64_t ore = 0;
                Money oreValue = 0;
                for (auto& [it, q] : f.cargo)
                    if (isOre(it)) { ore += q; oreValue += this->oreValue(f.corp, o.station, it) * q; }
                unloadAll(f, o.station, true);
                divFleetBook(f, oreValue, 0);
                if (f.corp == player && ore > 0) {
                    pstats.oreUnloaded += ore;
                    addLog(Chan::Corp, Sev::Info, true, stations[o.station].system,
                           f.name + " unloaded " + fmtInt(ore) + " ore at " + stations[o.station].name);
                }
                f.trips++;
                beginAct(f, Act::Unload, 5);
                f.status = "Unloading ore";
                f.phase = 0;
            }
            break;
        }

        case OrderKind::Haul: {
            if (f.phase == 0) {
                if (!goTo(f, o.station)) return;
                double loadedVol = 0;
                auto& h = corps[f.corp].hangar;
                std::vector<std::pair<int, int64_t>> want;
                for (auto it = h.lower_bound(hkey(o.station, 0)); it != h.end() && it->first < hkey(o.station + 1, 0); ++it) {
                    int i = (int)(it->first % HANGAR_KEY);
                    if (isShipItem(i)) continue;
                    if (o.item >= 0 && i != o.item) continue;
                    want.push_back({i, it->second});
                }
                int64_t limit = o.qty > 0 ? o.qty : INT64_MAX;
                for (auto& [it, q] : want) {
                    int64_t fit = (int64_t)(fleetFreeFor(f, it) / std::max(1e-9, item(it).volume));
                    int64_t n = std::min({q, fit, limit});
                    if (n <= 0) continue;
                    takeItem(f.corp, o.station, it, n);
                    f.cargo[it] += n;
                    loadedVol += n * item(it).volume;
                    limit -= n;
                }
                if (f.cargo.empty()) {
                    if (o.loop) { beginAct(f, Act::Wait, 60); f.status = "Waiting for cargo"; }
                    else { f.phase = 99; f.status = "Nothing to haul"; }
                    return;
                }
                insureCargo(*this, f, o.station2);
                beginAct(f, Act::Load, 10);
                f.status = "Loading " + fmtQty(loadedVol) + " m3";
                f.phase = 1;
            } else {
                if (!goTo(f, o.station2)) return;
                for (auto& [it, q] : f.cargo)
                    for (Id s : f.ships) ships[s].hauled += q * item(it).volume / f.ships.size();
                unloadAll(f, o.station2);
                f.cargoCover = 0;
                f.trips++;
                beginAct(f, Act::Unload, 10);
                f.status = "Unloading";
                if (f.corp == player)
                    addLog(Chan::Corp, Sev::Info, true, stations[o.station2].system, f.name + " delivered cargo to " + stations[o.station2].name);
                if (o.colony != NONE && o.colony < (Id)colonies.size()) {  // straight to the colonists
                    Result r = supplyColony(f.corp, o.colony);
                    if (f.corp == player && r.ok) addLog(Chan::Corp, Sev::Good, true, stations[o.station2].system, r.msg);
                }
                if (o.project != NONE && o.project < (Id)projects.size()) {  // straight into the project
                    Result r = contributeProject(f.corp, o.project, 0, true);
                    if (f.corp == player && r.ok) addLog(Chan::Corp, Sev::Good, true, stations[o.station2].system, r.msg);
                }
                if (o.contract != NONE && o.contract < (Id)contracts.size()) {
                    Money reward = contracts[o.contract].reward;
                    Result r = deliverContract(f.corp, o.contract);
                    if (r.ok) divFleetBook(f, reward, 0);
                    if (f.corp == player && r.ok) addLog(Chan::Corp, Sev::Good, true, stations[o.station2].system, r.msg);
                    o.contract = NONE;
                }
                f.phase = o.loop ? 0 : 99;
                if (f.phase == 99) f.status = "Haul complete";
            }
            break;
        }

        case OrderKind::Trade: {
            if (o.item < 0) { f.phase = 99; return; }
            if (f.phase == 0) {
                if (!goTo(f, o.station)) return;
                int64_t fit = (int64_t)(fleetFreeFor(f, o.item) / std::max(1e-9, item(o.item).volume));
                int64_t n = o.qty > 0 ? std::min(o.qty, fit) : fit;
                Money spent = 0;
                int64_t got = n > 0 ? instantBuy(f.corp, o.station, o.item, n, o.price, &spent) : 0;
                if (got > 0) {
                    takeItem(f.corp, o.station, o.item, got);
                    f.cargo[o.item] += got;
                }
                if (f.cargo[o.item] <= 0) {
                    f.cargo.erase(o.item);
                    if (o.loop) { beginAct(f, Act::Wait, 60); f.status = "Nothing at or under the price; waiting"; }
                    else { f.phase = 99; f.status = "Nothing to buy at or under the price"; }
                    return;
                }
                f.spent = spent;
                insureCargo(*this, f, o.station2);
                beginAct(f, Act::Load, 10);
                f.status = "Bought " + fmtInt(got) + " " + item(o.item).name;
                f.phase = 1;
            } else {
                if (!goTo(f, o.station2)) return;
                int64_t q = f.cargo.count(o.item) ? f.cargo[o.item] : 0;
                if (q > 0) {
                    f.cargo.erase(o.item);
                    addItem(f.corp, o.station2, o.item, q);
                    for (Id s : f.ships) ships[s].hauled += q * item(o.item).volume / f.ships.size();
                }
                f.cargoCover = 0;
                Money got = 0;
                int64_t sold = q > 0 ? instantSell(f.corp, o.station2, o.item, q, o.price2, &got) : 0;
                f.earned = got;
                f.trips++;
                Money profit = (Money)(got * (1.0 - salesTax(f.corp, o.station2))) - f.spent;
                divFleetBook(f, (Money)(got * (1.0 - salesTax(f.corp, o.station2))), f.spent);
                if (sold < q && f.division == DIV_TRADE && corps[f.corp].player) {
                    // the leftover is sold off later, and booked when it sells
                    corps[f.corp].divs[DIV_TRADE].leftovers.push_back(hkey(o.station2, o.item));
                }
                dbgTradeProfit[o.item] += toCr(profit);
                dbgTradeTrips[o.item] += 1;
                if (f.corp == player) {
                    pstats.tradeRuns++;
                    std::string msg = f.name + " sold " + fmtInt(sold) + " " + item(o.item).name + " at " +
                                      stations[o.station2].name + ", trip result " + fmtMoneyShort(profit);
                    if (sold < q) msg += "; " + fmtInt(q - sold) + " left in your hangar there";
                    addLog(Chan::Market, profit >= 0 ? Sev::Good : Sev::Warn, true, stations[o.station2].system, msg);
                }
                beginAct(f, Act::Unload, 10);
                f.status = "Sold " + fmtInt(sold) + "; result " + fmtMoneyShort(profit);
                f.phase = o.loop ? 0 : 99;
            }
            break;
        }

        case OrderKind::Survey: {
            if (o.system == NONE) { f.phase = 99; return; }
            if (!goToSystem(f, o.system)) return;
            double scan = 0;
            for (Id s : f.ships) scan = std::max(scan, stats(ships[s]).scan);
            beginAct(f, Act::Survey, (Tick)(480.0 / (0.4 + scan)));
            f.status = "Surveying " + systems[o.system].name;
            break;
        }

        case OrderKind::Salvage: {
            const Site* site = nullptr;
            for (auto& x : sites)
                if (x.id == o.site) site = &x;
            if (f.phase == 0) {
                if (!site || site->done || site->expires <= now) { f.phase = 99; f.status = "The site is gone"; return; }
                if (!goToSystem(f, site->system)) return;
                beginAct(f, Act::Salvage, site->kind == SiteKind::Relic ? 300 : 180);
                f.status = "Salvaging the " + site->name;
            } else {
                Id st = o.station != NONE ? o.station : nearestStation(f.system);
                if (st == NONE) { f.phase = 99; return; }
                if (!goTo(f, st)) return;
                Money haul = 0;
                for (auto& [it, q] : f.cargo) haul += valueAt(st, it) * q;
                unloadAll(f, st);
                divFleetBook(f, haul, 0);
                beginAct(f, Act::Unload, 10);
                f.status = "Salvage delivered to " + stations[st].name;
                f.phase = 99;
                if (f.corp == player) addLog(Chan::Corp, Sev::Info, true, stations[st].system, f.name + " delivered its salvage to " + stations[st].name + ".");
            }
            break;
        }

        case OrderKind::Scout: {
            if (!goToSystem(f, o.system)) return;
            beginAct(f, Act::Wait, 30);
            f.status = "Scouting " + systems[o.system].name;
            break;
        }

        case OrderKind::Patrol:
        case OrderKind::Raid:
        case OrderKind::Siege: {
            if (!goToSystem(f, o.system)) return;
            beginAct(f, Act::Wait, 30);
            if (o.kind == OrderKind::Patrol) f.status = "Patrolling " + systems[o.system].name;
            else if (o.kind == OrderKind::Raid) f.status = "Hunting in " + systems[o.system].name;
            else f.status = "Besieging " + (o.station != NONE ? stations[o.station].name : systems[o.system].name);
            break;
        }
        default: break;
    }
}

// --------------------------------------------------------------- encounters --
void World::scanForTargets(Fleet& f) {
    const Corp& me = corps[f.corp];
    StarSystem& sys = systems[f.system];
    double mine = fleetStrength(f);
    if (mine <= 0) return;
    bool ganker = me.arch == Arch::Ganker;
    bool pirate = me.arch == Arch::Pirate;
    const std::vector<Id>& here = fleetsIn[f.system];
    for (Id gid : here) {
        Fleet& g = fleets[gid];
        if (!g.alive || g.id == f.id || g.corp == f.corp || g.system != f.system) continue;
        if (g.docked != NONE || g.battle != NONE) continue;
        const Corp& them = corps[g.corp];
        double theirs = fleetStrength(g);
        bool engage = false;
        if (f.order.kind == OrderKind::Patrol || f.order.kind == OrderKind::Siege) {
            if (hostileTo(f.corp, g.corp) && mine >= (me.player ? 1.0 : 1.2) * theirs) engage = true;
        } else {  // Raid
            if (f.order.targetCorp != NONE && g.corp != f.order.targetCorp) continue;
            if (standing(f.corp, g.corp) >= 5.0f || allied(f.corp, g.corp) || nonAggression(f.corp, g.corp)) continue;
            if ((pirate || ganker) && (them.arch == Arch::Pirate || them.arch == Arch::Ganker)) continue;
            Money value = fleetCargoValue(g);
            for (Id s : g.ships) value += shipValue(ships[s]) * 3 / 10;
            double guard = theirs;
            for (Id hid : here) {
                const Fleet& h = fleets[hid];
                if (h.alive && h.corp == g.corp && h.id != g.id && h.docked == NONE && h.order.kind == OrderKind::Patrol)
                    guard += fleetStrength(h);
            }
            if (ganker) {
                double hullLeft = 0;
                for (Id s2 : g.ships) hullLeft += ships[s2].hull;
                bool lawful = sys.zone == Zone::Charter || (sys.zone == Zone::Contract && sys.depth == 1);
                engage = lawful && fleetCargoValue(g) >= 5000000 * CR && guard < 12 && mine * 1.8 >= hullLeft;
            } else if (pirate) {
                engage = sys.zone != Zone::Charter && value >= 300000 * CR && mine >= 1.3 * guard;
            } else if (me.player) {
                engage = mine >= guard && value > 0;
            } else {
                engage = mine >= 1.5 * guard && value >= 500000 * CR;
            }
        }
        if (!engage) continue;
        // sensors, the target's piloting and its evasion fittings decide whether contact is made this scan
        double nav = 0;
        for (Id s : g.ships) nav += (pilotFactor(ships[s], 1) - 0.8) / 0.4;
        nav /= std::max<size_t>(1, g.ships.size());
        double p = 0.25 * (1.2 - 0.4 * nav) * (1.0 - fleetEvasion(g));
        if (them.player) p *= 1.0 - 0.3 * execBonus(g.corp, SEAT_SECURITY);
        if (!rng.chance(p)) continue;
        startBattle(f.system, {f.id}, {g.id});
        return;
    }
}

int World::wardenRound(Id system) const {
    const StarSystem& s = systems[system];
    if (s.zone == Zone::Contract && s.beacon) return (s.beacon == 2 ? 2 : 3) + (int)(s.risk / 20.0f);  // a Charter Beacon's reach
    if (s.zone != Zone::Charter) return -1;
    if (s.region != NONE && s.region < (Id)crackdown.size() && crackdown[s.region]) return 1;
    return 1 + (int)(s.risk / 20.0f);
}

void World::startBattle(Id system, std::vector<Id> att, std::vector<Id> def) {
    Battle b;
    b.id = (Id)battles.size();
    b.system = system;
    b.attCorp = fleets[att[0]].corp;
    b.defCorp = fleets[def[0]].corp;
    StarSystem& s = systems[system];
    // friends of the defender who are patrolling here join in
    for (Id fid : fleetsIn[system]) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.system != system || f.docked != NONE || f.battle != NONE) continue;
        if (std::find(def.begin(), def.end(), f.id) != def.end() || std::find(att.begin(), att.end(), f.id) != att.end()) continue;
        if (f.order.kind != OrderKind::Patrol) continue;
        if (f.corp == b.defCorp || hostileTo(f.corp, b.attCorp) || (allied(f.corp, b.defCorp) && !allied(f.corp, b.attCorp))) def.push_back(f.id);
    }
    b.att = att;
    b.def = def;
    // fighting an outlaw is lawful; so is fighting a corporation you hold a war licence against
    b.lawful = (isOutlaw(b.defCorp) && !isOutlaw(b.attCorp)) || atWar(b.attCorp, b.defCorp);
    const Corp& ac = corps[b.attCorp];
    if (ac.arch != Arch::Pirate && ac.arch != Arch::Ganker) noteAggression(b.defCorp, b.attCorp);
    if (!b.lawful) {
        b.policeRound = wardenRound(system);
        float hit = s.zone == Zone::Charter ? 2.5f : s.zone == Zone::Contract ? 0.5f : 0.0f;
        corps[b.attCorp].charterStanding = std::max(-10.0f, corps[b.attCorp].charterStanding - hit);
    }
    corps[b.defCorp].standing[b.attCorp] = std::max(-10.0f, standing(b.defCorp, b.attCorp) - 3.0f);
    for (Id f : b.att) { fleets[f].battle = b.id; fleets[f].act = Act::Combat; b.attStart += fleetStrength(fleets[f]); }
    for (Id f : b.def) { fleets[f].battle = b.id; fleets[f].act = Act::Combat; }
    s.heat += 1.0f;
    bool mine = b.attCorp == player || b.defCorp == player;
    for (Id f : b.def) mine = mine || fleets[f].corp == player;
    addLog(Chan::Combat, mine ? Sev::Bad : Sev::Warn, mine, system,
           "Combat in " + s.name + ": " + corps[b.attCorp].name + " attacks " + corps[b.defCorp].name + " (" + fleets[def[0]].name +
               ")" + (b.policeRound > 0 ? ". Charter Wardens are responding." : "."));
    battles.push_back(b);
}

void World::destroyShip(Id sid, Id killer, Battle* b) {
    Ship& s = ships[sid];
    if (!s.alive) return;
    Corp& owner = corps[s.corp];
    Money value = shipValue(s);
    Id where = b ? b->system : (s.fleet != NONE ? fleets[s.fleet].system : stations[s.station].system);
    Money cargoLost = 0;
    if (s.fleet != NONE) {
        Fleet& f = fleets[s.fleet];
        divFleetBook(f, 0, value);  // a lost hull is a division's cost
        ShipStats st = stats(s);
        double cap = fleetCargoCap(f) + fleetOreCap(f);
        double share = cap > 0 ? (st.cargo + st.oreHold) / cap : 1.0;
        if (f.ships.size() == 1) share = 1.0;
        for (auto it = f.cargo.begin(); it != f.cargo.end();) {
            int64_t drop = (int64_t)std::ceil(it->second * share);
            drop = std::min(drop, it->second);
            if (b) b->loot[it->first] += drop / 2;
            cargoLost += item(it->first).base * drop;
            it->second -= drop;
            if (it->second <= 0) it = f.cargo.erase(it); else ++it;
        }
        payCargoCover(f, cargoLost);
        f.ships.erase(std::remove(f.ships.begin(), f.ships.end(), sid), f.ships.end());
        if (f.ships.empty()) f.alive = false;
    }
    if (b) b->valueLost += value + cargoLost;
    s.alive = false;
    s.fleet = NONE;
    owner.shipsLost++;
    if (killer != NONE) {
        corps[killer].shipsKilled++;
        if (b) {
            for (Id fid : b->att)
                if (fleets[fid].corp == killer && !fleets[fid].ships.empty()) { ships[fleets[fid].ships[0]].kills++; break; }
            for (Id fid : b->def)
                if (fleets[fid].corp == killer && !fleets[fid].ships.empty()) { ships[fleets[fid].ships[0]].kills++; break; }
        }
    }
    StarSystem& sys = systems[where];
    sys.heat += 0.5f;
    sys.losses += (float)toCr(value + cargoLost) / 1e6f;
    // war score
    if (killer != NONE && atWar(killer, s.corp))
        if (Treaty* war = findTreaty(Pact::War, killer, s.corp)) (war->a == killer ? war->scoreA : war->scoreB) += value + cargoLost;
    // bounty contracts
    for (auto& c : contracts)
        if (c.state == ContractState::Accepted && c.kind == ContractKind::Bounty && c.contractor == killer && c.target == s.corp) c.progress++;
    if (s.pilot != NONE) {
        Person& p = people[s.pilot];
        double survive = 0.6 + 0.3 * p.nav / 100.0 + (p.trait == Trait::Steady ? 0.05 : 0.0);
        p.ship = NONE;
        if (rng.chance(survive)) {
            p.injuredUntil = now + 3 * MINUTES_PER_DAY;
            if (owner.player) addLog(Chan::Combat, Sev::Warn, true, where, p.name + " ejected and survived; injured for 3 days.");
        } else {
            p.alive = false;
            p.employer = NONE;
            for (auto& e : owner.exec)
                if (e == p.id) e = NONE;
            if (owner.player) addLog(Chan::Combat, Sev::Bad, true, where, p.name + " was killed in action.");
        }
        s.pilot = NONE;
    }
    if (s.insured && s.insuredUntil > now)
        pay(s.corp, value * 70 / 100, Cat::InsurancePayout, "Hull insurance payout: " + shipDef(s.def).name);
    if (!uw.book.empty()) uwClaim(s);
    if (killer != NONE && isOutlaw(s.corp) && !isOutlaw(killer)) {
        bool doubled = sys.region != NONE && sys.region < (Id)crackdown.size() && crackdown[sys.region];
        pay(killer, value / (doubled ? 2 : 4), Cat::Bounty, "Charter bounty: " + owner.name + " " + shipDef(s.def).name + (doubled ? " (crackdown)" : ""));
    }
    if (owner.player || killer == player)
        addLog(Chan::Combat, owner.player ? Sev::Bad : Sev::Good, true, where,
               owner.name + " lost " + (s.name.empty() ? "a " + shipDef(s.def).name : s.name + " (" + shipDef(s.def).name + ")") + " in " +
                   sys.name + " (" + fmtMoneyShort(value) + ")");
}

void World::battleTick(Battle& b) {
    auto alive = [&](std::vector<Id>& side) {
        side.erase(std::remove_if(side.begin(), side.end(), [&](Id f) { return !fleets[f].alive || fleets[f].battle != b.id; }), side.end());
    };
    alive(b.att);
    alive(b.def);
    if (b.att.empty() || b.def.empty()) { endBattle(b); return; }
    b.round++;
    auto shipsOf = [&](const std::vector<Id>& side) {
        std::vector<Id> v;
        for (Id f : side)
            for (Id s : fleets[f].ships) v.push_back(s);
        return v;
    };
    std::vector<Id> A = shipsOf(b.att), D = shipsOf(b.def);
    if (A.empty() || D.empty()) { endBattle(b); return; }
    std::vector<std::pair<Id, double>> hits;
    auto volley = [&](const std::vector<Id>& from, const std::vector<Id>& to) {
        for (Id s : from) {
            const Ship& sh = ships[s];
            double dmg = stats(sh).attack * pilotFactor(sh, 2) * rng.range(0.7, 1.3);
            if (corps[sh.corp].player) dmg *= 1.0 + 0.1 * execBonus(sh.corp, SEAT_SECURITY);
            hits.push_back({to[rng.irange(0, (int)to.size() - 1)], dmg});
        }
    };
    volley(A, D);
    volley(D, A);
    for (auto& [s, dmg] : hits) {
        Ship& sh = ships[s];
        if (!sh.alive) continue;
        sh.hull -= dmg;
        if (sh.hull <= 0) {
            bool attackerShip = std::find(A.begin(), A.end(), s) != A.end();
            if (attackerShip) b.attLost++; else b.defLost++;
            destroyShip(s, attackerShip ? b.defCorp : b.attCorp, &b);
        }
    }
    // the Charter Wardens
    if (b.policeRound > 0 && b.round >= b.policeRound) {
        std::vector<Id> doomed;
        for (Id f : b.att)
            for (Id s : fleets[f].ships) doomed.push_back(s);
        for (Id s : doomed) { b.attLost++; destroyShip(s, NONE, &b); }
        bool mine = b.attCorp == player || b.defCorp == player;
        addLog(Chan::Combat, Sev::Info, mine, b.system, "Charter Wardens arrived in " + systems[b.system].name + " and destroyed the aggressors.");
    }
    alive(b.att);
    alive(b.def);
    // defenders that are not warships try to slip away
    for (Id fid : std::vector<Id>(b.def)) {
        Fleet& f = fleets[fid];
        if (f.order.kind == OrderKind::Patrol || f.order.kind == OrderKind::Raid || f.order.kind == OrderKind::Siege) continue;
        double nav = 0;
        for (Id s : f.ships) nav += (pilotFactor(ships[s], 1) - 0.8) / 0.4;
        nav /= std::max<size_t>(1, f.ships.size());
        if (rng.chance(0.08 + 0.12 * nav + 0.4 * fleetEvasion(f))) {
            f.battle = NONE;
            f.act = Act::Idle;
            b.def.erase(std::remove(b.def.begin(), b.def.end(), fid), b.def.end());
            if (f.corp == player) addLog(Chan::Combat, Sev::Warn, true, b.system, f.name + " broke contact and escaped.");
        }
    }
    double nowAtt = 0;
    for (Id f : b.att) nowAtt += fleetStrength(fleets[f]);
    if (!b.att.empty() && nowAtt < 0.5 * b.attStart) {
        for (Id f : b.att) { fleets[f].battle = NONE; fleets[f].act = Act::Idle; }
        b.att.clear();
    }
    if (b.att.empty() || b.def.empty() || b.round > 60) endBattle(b);
}

void World::endBattle(Battle& b) {
    if (!b.active) return;
    b.active = false;
    Money lootValue = 0;
    for (auto& [it, q] : b.loot) lootValue += item(it).base * q;
    std::string lootNote;
    if (!b.loot.empty()) {
        if (!b.att.empty()) {
            Fleet& f = fleets[b.att[0]];
            int64_t taken = 0;
            for (auto& [it, q] : b.loot) {
                int64_t fit = (int64_t)(fleetFreeFor(f, it) / std::max(1e-9, item(it).volume));
                int64_t n = std::min(q, fit);
                if (n > 0) { f.cargo[it] += n; taken += n; }
            }
            if (taken) lootNote = "; " + corps[f.corp].name + " looted " + fmtMoneyShort(lootValue);
        } else if (corps[b.attCorp].arch == Arch::Ganker) {
            Id st = nearestStation(b.system);
            if (st != NONE)
                for (auto& [it, q] : b.loot) addItem(b.attCorp, st, it, q);
            lootNote = "; the wreck was looted";
        }
    }
    for (Id f : b.att) { fleets[f].battle = NONE; if (fleets[f].act == Act::Combat) fleets[f].act = Act::Idle; }
    for (Id f : b.def) { fleets[f].battle = NONE; if (fleets[f].act == Act::Combat) fleets[f].act = Act::Idle; }
    for (Id fid : fleetsIn[b.system]) {
        Fleet& f = fleets[fid];
        if (f.battle == b.id) { f.battle = NONE; if (f.act == Act::Combat) f.act = Act::Idle; }
    }
    bool mine = b.attCorp == player || b.defCorp == player;
    if (mine || b.valueLost > 20000000 * CR)
        addLog(Chan::Combat, mine ? Sev::Warn : Sev::Info, mine, b.system,
               "Battle in " + systems[b.system].name + " over: " + corps[b.attCorp].ticker + " lost " + std::to_string(b.attLost) + ", " +
                   corps[b.defCorp].ticker + " lost " + std::to_string(b.defLost) + lootNote + ".");
}

}  // namespace nb
