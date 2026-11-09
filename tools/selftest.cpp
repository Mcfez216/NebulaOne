// nebula_selftest - runs the simulation headless and prints economy health.
//
//   nebula_selftest [days] [seed] [--play] [--size compact|standard|vast|galaxy] [--quiet]
//
// --play scripts a simple player: mine with the Mattock, refine, sell, and
// run the Drover on trade runs, so the core money loop is exercised too.
// --size picks the chart; the run reports generation time, speed and memory,
// which is how the scale targets for big charts are checked.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "core/world.h"

using namespace nb;

static double rssMB() {
#ifdef __linux__
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) return std::atof(line.c_str() + 6) / 1024.0;
#endif
    return 0;
}

static double priceIndex(const World& w, int first, int last) {
    double s = 0;
    int n = 0;
    for (auto& st : w.stations) {
        if (st.dead) continue;
        for (int i = first; i < last; i++) {
            if (st.target[i] <= 0) continue;
            s += (double)w.refPrice(st.id, i) / (double)item(i).base;
            n++;
        }
    }
    return n ? s / n : 0;
}

static void spread(const World& w, int it, double& lo, double& hi) {
    lo = 1e9;
    hi = 0;
    for (auto& st : w.stations) {
        if (st.target[it] <= 0 || st.dead) continue;
        double r = (double)w.refPrice(st.id, it) / (double)item(it).base;
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
}

static void playerScript(World& w, int day) {
    Corp& p = w.pc();
    Id home = p.home;
    Id homeSys = w.stations[home].system;
    if (day == 0 && w.now == 1) {
        Id mattock = NONE, drover = NONE;
        for (Id sid : p.shipList) (w.ships[sid].def == MATTOCK ? mattock : drover) = sid;
        Command c;
        c.type = Cmd::CreateFleet; c.corp = w.player; c.list = {mattock}; c.text = "Prospector";
        Result r = w.execute(c);
        Id fleet = r.id;
        Id bsys = homeSys;
        if (w.systems[homeSys].belts.empty()) bsys = w.systems[homeSys].lanes[0].to;
        Command o;
        o.type = Cmd::SetFleetOrder; o.corp = w.player; o.a = fleet;
        o.order.kind = OrderKind::Mine; o.order.system = bsys; o.order.belt = 0; o.order.station = home;
        r = w.execute(o);
        std::printf("  [player] mine order: %s\n", r.msg.c_str());
        c.list = {drover}; c.text = "Mule";
        r = w.execute(c);
        std::printf("  [player] form hauler fleet: %s (pilots auto-assigned)\n", r.msg.c_str());
    }
    if (w.now % (MINUTES_PER_DAY / 4) == 0) {
        for (int o = 0; o < ORE_COUNT; o++) {
            int64_t q = w.have(w.player, home, o);
            if (q >= 100) {
                Command c; c.type = Cmd::Refine; c.corp = w.player; c.a = home; c.item = o; c.qty = q;
                Result r = w.execute(c);
                if (!r.ok) std::printf("  [player] refine failed: %s\n", r.msg.c_str());
            }
        }
        for (int m = MINERAL_FIRST; m < GOOD_FIRST; m++) {
            int64_t q = w.have(w.player, home, m);
            if (q > 0) {
                Command c; c.type = Cmd::MarketSell; c.corp = w.player; c.a = home; c.item = m; c.qty = q; c.price = 1;
                w.execute(c);
            }
        }
        for (Id fid : p.fleetList) {
            Fleet& f = w.fleets[fid];
            if (!f.alive || f.name != "Mule") continue;
            bool idle = f.order.kind == OrderKind::None || f.phase == 99;
            if (!idle || f.act != Act::Idle) continue;
            double best = 0; Id A = NONE, B = NONE; int bi = -1; Money ask0 = 0, bid0 = 0;
            Id loc = f.docked != NONE ? w.stations[f.docked].system : f.system;
            std::vector<Id> srcs, dsts;
            for (Id sy : w.systemsWithin(loc, 2))
                if (w.systems[sy].zone == Zone::Charter) for (Id st : w.systems[sy].stations) srcs.push_back(st);
            for (Id sy : w.systemsWithin(loc, 6))
                if (w.systems[sy].zone == Zone::Charter) for (Id st : w.systems[sy].stations) dsts.push_back(st);
            for (Id a : srcs)
                for (int it = GOOD_FIRST; it < SHIP_FIRST; it++) {
                    int64_t da; Money ask = w.bestAsk(a, it, &da);
                    if (!ask) continue;
                    for (Id b : dsts) {
                        if (b == a) continue;
                        int64_t db; Money bid = w.bestBid(b, it, &db);
                        if (bid <= ask) continue;
                        int64_t q = std::min({da, db, (int64_t)(w.fleetCargoCap(f) / item(it).volume), p.wallet / ask});
                        double prof = q * ((double)bid * 0.965 - ask);
                        if (prof > best) { best = prof; A = a; B = b; bi = it; ask0 = ask; bid0 = bid; }
                    }
                }
            if (A != NONE) {
                Command c; c.type = Cmd::SetFleetOrder; c.corp = w.player; c.a = f.id;
                c.order.kind = OrderKind::Trade; c.order.station = A; c.order.station2 = B; c.order.item = bi;
                c.order.price = ask0 * 103 / 100; c.order.price2 = bid0 * 95 / 100;
                w.execute(c);
            }
        }
    }
}

// Exercises every new rule through World::execute, the way the UI does.
static int featureTest(uint64_t seed) {
    World w;
    NewGameSetup s;
    s.seed = seed;
    s.size = GalaxySize::Standard;
    w.generate(s);
    int fails = 0;
    auto check = [&](const char* what, bool ok, const std::string& msg = "") {
        std::printf("  %-44s %s  %s\n", what, ok ? "ok  " : "FAIL", msg.c_str());
        fails += !ok;
    };
    auto run = [&](Command c) { c.corp = w.player; return w.execute(c); };
    Corp& p = w.pc();
    Id home = p.home;
    p.wallet += 400000000 * CR;
    std::printf("\nfeature checks (Standard chart, seed %llu)\n", (unsigned long long)seed);
    // fitting
    Id l = w.spawnShip(w.player, LANCER, home);
    w.addItem(w.player, home, MOD_WEAPONS, 1);
    double a0 = w.stats(w.ships[l]).attack;
    Command c; c.type = Cmd::FitModule; c.a = l; c.b = 0; c.item = MOD_WEAPONS;
    Result r = run(c);
    check("fit a Weapon Battery to a Lancer", r.ok && w.stats(w.ships[l]).attack > a0 * 1.3, r.msg);
    c.type = Cmd::UnfitModule;
    r = run(c);
    check("remove it again", r.ok && w.have(w.player, home, MOD_WEAPONS) == 1, r.msg);
    // research and copying
    w.addItem(w.player, home, BPO_FIRST + BP_COMPONENT_FIRST, 1);
    c = Command{}; c.type = Cmd::Research; c.a = home; c.item = BP_COMPONENT_FIRST; c.qty = 2;
    r = run(c);
    check("research Hull Plating ME +2", r.ok, r.msg);
    w.addItem(w.player, home, BPO_FIRST + BP_SHIP_FIRST + DROVER, 1);
    c = Command{}; c.type = Cmd::CopyBlueprint; c.a = home; c.item = BP_SHIP_FIRST + DROVER; c.qty = 2;
    r = run(c);
    check("copy the Drover blueprint twice", r.ok, r.msg);
    w.advance(3 * MINUTES_PER_DAY);
    check("research finished", p.me[BP_COMPONENT_FIRST] == 2, "ME " + std::to_string(p.me[BP_COMPONENT_FIRST]));
    check("copies delivered", w.have(w.player, home, BPC_FIRST + DROVER) == 2);
    // manufacture components from minerals, then a hull from a copy
    const BlueprintDef& hp = blueprint(BP_COMPONENT_FIRST);
    for (auto& [m, q] : hp.materials) w.addItem(w.player, home, m, q * 3);
    c = Command{}; c.type = Cmd::Manufacture; c.a = home; c.item = BP_COMPONENT_FIRST; c.qty = 3;
    r = run(c);
    check("build 3 runs of Hull Plating (ME saves material)", r.ok, r.msg);
    const BlueprintDef& dv = blueprint(BP_SHIP_FIRST + DROVER);
    w.takeItem(w.player, home, BPO_FIRST + BP_SHIP_FIRST + DROVER, 1);  // force the copy path
    for (auto& [m, q] : dv.materials) w.addItem(w.player, home, m, q);
    int64_t drovers0 = w.have(w.player, home, SHIP_DROVER);
    c = Command{}; c.type = Cmd::Manufacture; c.a = home; c.item = BP_SHIP_FIRST + DROVER; c.qty = 1;
    r = run(c);
    check("build a Drover from a copy", r.ok && w.have(w.player, home, BPC_FIRST + DROVER) == 1, r.msg);
    w.advance(MINUTES_PER_DAY);
    check("hull delivered", w.have(w.player, home, SHIP_DROVER) == drovers0 + 1);
    // contracts
    Id sup = NONE, cour = NONE;
    for (auto& k : w.contracts) {
        if (k.state != ContractState::Open) continue;
        if (k.kind == ContractKind::Supply && sup == NONE) sup = k.id;
        if (k.kind == ContractKind::Courier && cour == NONE) cour = k.id;
    }
    if (sup != NONE) {
        c = Command{}; c.type = Cmd::AcceptContract; c.a = sup;
        r = run(c);
        check("accept a supply contract", r.ok, r.msg);
        Contract k = w.contracts[sup];
        w.addItem(w.player, k.to, k.item, k.qty);
        Money before = p.wallet;
        c.type = Cmd::DeliverContract;
        r = run(c);
        check("deliver it and get paid", r.ok && p.wallet > before, r.msg);
    } else check("a supply contract on the board", false);
    if (cour != NONE) {
        c = Command{}; c.type = Cmd::AcceptContract; c.a = cour;
        Money before = p.wallet;
        r = run(c);
        const Contract& k = w.contracts[cour];
        check("accept courier work: goods handed over", r.ok && w.have(w.player, k.from, k.item) >= k.qty, r.msg);
        check("collateral held", p.wallet == before - k.collateral);
    }
    c = Command{}; c.type = Cmd::PostContract; c.contract.kind = ContractKind::Bounty; c.contract.reward = 5000000 * CR; c.contract.goal = 2;
    for (auto& oc : w.corps) if (w.isOutlaw(oc.id)) { c.contract.target = oc.id; break; }
    r = run(c);
    check("post a bounty on an outlaw corporation", r.ok, r.msg);
    // an outpost in the Frontier
    Id carrack = w.spawnShip(w.player, CARRACK, home);
    Id fid = w.createFleet(w.player, {carrack}, "Settler");
    Fleet& f = w.fleets[fid];
    f.cargo[OUTPOST_KIT] = 1;
    Id target = NONE;
    for (auto& sy : w.systems) if (sy.zone == Zone::Frontier && sy.owner == NONE && sy.stations.empty()) { target = sy.id; break; }
    f.docked = NONE;
    f.system = target;
    c = Command{}; c.type = Cmd::AnchorOutpost; c.a = fid;
    r = run(c);
    check("anchor an outpost in the Frontier", r.ok, r.msg);
    Id op = r.id;
    if (r.ok) {
        w.addItem(w.player, op, FUEL_CELLS, 2000);
        w.advance(25 * MINUTES_PER_HOUR);
        check("anchoring completes: outpost online", w.stations[op].state == OutpostState::Online, outpostStateName(w.stations[op].state));
        check("sovereignty over the system", w.systems[target].owner == w.player);
        // a pirate band besieges it
        Id pir = NONE;
        for (auto& oc : w.corps) if (oc.arch == Arch::Pirate) { pir = oc.id; break; }
        std::vector<Id> band;
        for (int k = 0; k < 6; k++) band.push_back(w.spawnShip(pir, WARDEN, home));
        Id bf = w.createFleet(pir, band, "Siege band");
        w.fleets[bf].docked = NONE;
        w.fleets[bf].system = target;
        FleetOrder so; so.kind = OrderKind::Siege; so.station = op; so.system = target;
        w.setOrder(w.fleets[bf], so);
        w.corps[pir].nextThink = w.now + 100000;  // keep the AI from changing its mind for the test
        w.advance(6 * MINUTES_PER_HOUR);
        check("a siege takes the shield down", w.stations[op].shield < 0.9, "shield " + std::to_string(w.stations[op].shield));
    }
    // the corporate game
    w.advance(30 * MINUTES_PER_DAY);
    p.netWorth = w.computeNetWorth(w.player);
    c = Command{}; c.type = Cmd::GoPublic; c.f = 0.25f;
    Money before = p.wallet;
    r = run(c);
    check("IPO: float 25% on the Exchange", r.ok && p.listed && p.wallet > before, r.msg);
    c = Command{}; c.type = Cmd::DeclareDividend; c.price = 1000000 * CR;
    r = run(c);
    check("declare a dividend", r.ok, r.msg);
    c = Command{}; c.type = Cmd::Buyback; c.qty = 10000;
    r = run(c);
    check("buy back shares", r.ok, r.msg);
    Id victim = NONE;
    for (auto& oc : w.corps) if (oc.alive && oc.arch == Arch::Trader) { victim = oc.id; break; }
    int theirShips = 0;
    for (Id sid : w.corps[victim].shipList) theirShips += w.ships[sid].alive && w.ships[sid].corp == victim;
    p.shares[victim] = w.corps[victim].sharesOut * 6 / 10;
    p.wallet += w.corps[victim].sharePrice * w.corps[victim].sharesOut;
    int mine0 = (int)p.shipList.size();
    c = Command{}; c.type = Cmd::Merge; c.a = victim;
    r = run(c);
    check("take over an AI trader with 60% of its shares", r.ok && !w.corps[victim].alive, r.msg);
    check("its ships are now yours", (int)p.shipList.size() >= mine0 + theirShips);
    auto crew = [&](Id ship) {  // test hulls need pilots before they fly
        Id pid = w.genPeople(home, 1, 0);
        w.people[pid].employer = w.player;
        w.people[pid].ship = ship;
        w.ships[ship].pilot = pid;
    };
    // ---- fog of war and intel
    {
        Id homeSys = w.stations[home].system;
        Id far = NONE;
        for (auto& st : w.stations)
            if (!st.dead && !st.isOutpost() && w.jumps(homeSys, st.system) >= 6 && !w.seen(st.system) &&
                w.systems[st.system].zone != Zone::Charter) { far = st.id; break; }
        check("fog: a far station is out of sight", far != NONE && !w.seen(w.stations[far].system));
        c = Command{}; c.type = Cmd::MarketBuy; c.a = far; c.item = FUEL_CELLS; c.qty = 10;
        r = run(c);
        check("no instant trades without a live feed", !r.ok, r.msg);
        Id reg = w.systems[w.stations[far].system].region;
        c = Command{}; c.type = Cmd::BuyIntel; c.a = reg; c.flag = false;
        r = run(c);
        check("buy a regional market report", r.ok && w.intel.count(far) && w.intel[far].t == w.now, r.msg);
        c.flag = true;
        r = run(c);
        check("buy a day of fleet sightings", r.ok && w.sighted(w.stations[far].system) && !w.seen(w.stations[far].system), r.msg);
        Id scout = w.spawnShip(w.player, WISP, home);
        Id sf = w.createFleet(w.player, {scout}, "Eye");
        w.fleets[sf].docked = NONE;
        w.fleets[sf].system = w.stations[far].system;
        w.updateSight();
        int lit = 0;
        int ring1 = (int)w.systemsWithin(w.stations[far].system, 1).size();
        for (Id n : w.systemsWithin(w.stations[far].system, 1)) lit += w.seen(n);
        check("a Wisp sees a jump around it", lit == ring1, std::to_string(lit) + " systems");
        // ---- exploration: survey Frontier systems until something turns up
        Id wisp2 = w.spawnShip(w.player, WISP, home);
        w.addItem(w.player, home, MOD_SCANNER, 1);
        c = Command{}; c.type = Cmd::FitModule; c.a = wisp2; c.b = 0; c.item = MOD_SCANNER;
        r = run(c);
        check("fit a Survey Scanner to a Wisp", r.ok && w.stats(w.ships[wisp2]).scan > 1.7, r.msg);
        crew(wisp2);
        Id ef = w.createFleet(w.player, {wisp2}, "Pathfinder");
        int found0 = (int)w.pstats.sitesFound;
        Id derelict = NONE;
        for (auto& sy : w.systems) {
            if (sy.zone != Zone::Frontier) continue;
            Fleet& e = w.fleets[ef];
            e.docked = NONE;
            e.system = sy.id;
            e.act = Act::Idle;
            FleetOrder so; so.kind = OrderKind::Survey; so.system = sy.id;
            w.setOrder(e, so);
            w.advance(5 * MINUTES_PER_HOUR);
            for (auto& st : w.sites)
                if (st.finder == w.player && !st.done && (st.kind == SiteKind::Derelict || st.kind == SiteKind::Relic)) derelict = st.id;
            if (derelict != NONE && w.pstats.sitesFound >= found0 + 3) break;
        }
        {
            Fleet& e = w.fleets[ef];
            int lit2 = 0, ring2 = (int)w.systemsWithin(e.system, 2).size();
            w.updateSight();
            for (Id n : w.systemsWithin(e.system, 2)) lit2 += w.seen(n);
            check("with a Survey Scanner it sees two jumps", lit2 == ring2, std::to_string(lit2) + " of " + std::to_string(ring2));
        }
        check("surveys find hidden sites", w.pstats.sitesFound > found0, std::to_string(w.pstats.sitesFound - found0) + " found");
        if (derelict != NONE) {
            const Site* st = nullptr;
            for (auto& x : w.sites) if (x.id == derelict) st = &x;
            Id hauler = w.spawnShip(w.player, DROVER, home);
            crew(hauler);
            Id hf = w.createFleet(w.player, {hauler}, "Salvor");
            w.fleets[hf].docked = NONE;
            w.fleets[hf].system = st->system;
            c = Command{}; c.type = Cmd::SetFleetOrder; c.a = hf; c.order.kind = OrderKind::Salvage; c.order.site = derelict; c.order.station = NONE;
            r = run(c);
            int64_t salv0 = w.pstats.salvaged;
            w.advance(6 * MINUTES_PER_HOUR);
            check("salvage a derelict or relic cache", r.ok && w.pstats.salvaged == salv0 + 1, r.msg);
        }
        int pockets = 0, hidden = 0;
        Id other = w.player == 0 ? 1 : 0;
        for (auto& st : w.sites)
            if (st.kind == SiteKind::Pocket && st.finder == w.player) {
                const Belt& b = w.systems[st.system].belts[st.belt];
                pockets++;
                hidden += w.beltVisible(b, w.player) && !w.beltVisible(b, other);
            }
        check("hidden ore pockets stay hidden from rivals", hidden == pockets, std::to_string(pockets) + " pockets found");
    }
    // ---- galaxy events
    {
        Id ind = NONE;
        for (auto& st : w.stations) if (!st.dead && st.type == StationType::Industrial) { ind = st.id; break; }
        w.postEvent(EventKind::Strike, ind, -1, 0, 3, 0);
        check("a strike stops production", w.stations[ind].prodMul == 0.0f);
        Id hub = NONE;
        for (Id h : w.hubs) if (w.systems[w.stations[h].system].zone == Zone::Charter) { hub = h; break; }
        w.postEvent(EventKind::Embargo, NONE, ELECTRONICS, 0, 5, 1);
        c = Command{}; c.type = Cmd::PlaceOrder; c.a = hub; c.item = ELECTRONICS; c.flag = true; c.price = 100 * CR; c.qty = 10;
        r = run(c);
        check("an embargo closes Charter markets to the item", !r.ok && w.embargoed(hub, ELECTRONICS), r.msg);
        Id sys = w.stations[home].system;
        Id nb = w.systems[sys].lanes[0].to;
        w.postEvent(EventKind::Storm, nb, -1, 0, 12, 1);
        check("an ion storm closes a system's lanes", w.stormBlocked(nb));
        w.advance(4 * MINUTES_PER_DAY);
        check("the strike ends and production resumes", w.stations[ind].prodMul == 1.0f);
        check("the storm has passed", !w.stormBlocked(nb));
    }
    // ---- wars and diplomacy
    {
        Id rival = NONE, merc = NONE, friendCorp = NONE;
        for (auto& oc : w.corps) {
            if (!oc.alive || oc.player) continue;
            if (oc.arch == Arch::Industrialist && rival == NONE && !w.allied(oc.id, w.player)) rival = oc.id;
            if (oc.arch == Arch::Mercenary && merc == NONE) merc = oc.id;
            if (oc.arch == Arch::Trader && friendCorp == NONE) {
                int pacts = 0;  // one that still has room for a pact
                for (auto& t : w.treaties) pacts += t.active && t.kind == Pact::NonAggression && (t.a == oc.id || t.b == oc.id);
                if (pacts < 2) friendCorp = oc.id;
            }
        }
        Money fee = w.warFee(rival);
        before = p.wallet;
        c = Command{}; c.type = Cmd::DeclareWar; c.a = rival;
        r = run(c);
        check("declare war (the Charter sells a licence)", r.ok && p.wallet == before - fee, r.msg);
        check("hostilities wait a day", !w.atWar(w.player, rival));
        w.advance(MINUTES_PER_DAY + 61);
        check("then the war is live", w.atWar(w.player, rival) && w.hostileTo(w.player, rival));
        // a fight in Charter space is lawful between licensed enemies: no Wardens, no Charter standing lost
        Id hs = w.stations[home].system;
        Id a1 = w.spawnShip(w.player, WARDEN, home), d1 = w.spawnShip(rival, DROVER, home);
        Id af = w.createFleet(w.player, {a1}, "Licensed"), df = w.createFleet(rival, {d1}, "Prey");
        w.fleets[af].docked = NONE; w.fleets[df].docked = NONE;
        float cs = p.charterStanding;
        w.startBattle(hs, {af}, {df});
        const Battle& b = w.battles.back();
        check("war in Charter space is lawful", b.lawful && b.policeRound < 0 && p.charterStanding == cs);
        w.advance(30);
        Treaty* war = w.findTreaty(Pact::War, w.player, rival);
        check("kills count toward the war score", war && war->scoreA > 0, war ? fmtMoneyShort(war->scoreA) : "");
        c = Command{}; c.type = Cmd::ProposePeace; c.a = rival; c.price = 0;
        w.advance(2 * MINUTES_PER_DAY);
        r = run(c);
        check("the loser will not pay; a white peace is fine", r.ok && !w.atWar(w.player, rival), r.msg);
        w.corps[friendCorp].standing[w.player] = 3.0f;
        c = Command{}; c.type = Cmd::ProposePact; c.a = friendCorp; c.flag = false;
        r = run(c);
        check("sign a non-aggression pact", r.ok && w.nonAggression(w.player, friendCorp), r.msg);
        c.flag = true;
        r = run(c);
        check("an alliance needs more trust", !r.ok, r.msg);
        w.corps[friendCorp].standing[w.player] = 8.0f;
        r = run(c);
        check("allied once standing is high", r.ok && w.allied(w.player, friendCorp), r.msg);
        Id target2 = NONE;
        for (auto& oc : w.corps)
            if (oc.alive && !oc.player && oc.arch == Arch::Miner && !w.allied(oc.id, merc) && !w.nonAggression(oc.id, merc)) { target2 = oc.id; break; }
        Money q = w.mercQuote(merc, 7);
        before = p.wallet;
        c = Command{}; c.type = Cmd::HireMercs; c.a = merc; c.b = target2; c.qty = 7;
        r = run(c);
        check("hire mercenaries against a rival", r.ok && p.wallet == before - q && w.findTreaty(Pact::War, merc, target2, false), r.msg);
    }
    // ---- divisions and directors
    {
        for (auto& t : w.treaties)  // start from peace: the war checks above leave enemies behind
            if (t.active && t.kind == Pact::War && (t.a == w.player || t.b == w.player)) w.endTreaty(t, "test");
        w.rebuildPacts();
        auto hireExec = [&](int seat) -> Id {
            Id cand = NONE;
            for (int pass = 0; pass < 2 && cand == NONE; pass++) {
                for (auto& pp : w.people)
                    if (pp.alive && pp.employer == NONE && pp.hub != NONE && pp.role == Role::Executive) { cand = pp.id; break; }
                if (cand == NONE) w.genPeople(w.hubs[0], 0, 3);
            }
            Command h; h.type = Cmd::Hire; h.a = cand;
            if (!run(h).ok) return NONE;
            h = Command{}; h.type = Cmd::AssignExec; h.a = cand; h.b = seat;
            return run(h).ok ? cand : NONE;
        };
        auto automate = [&](int div, DivPolicy pol) {
            pol.automate = true;
            Command k; k.type = Cmd::SetDivision; k.a = div; k.policy = pol;
            return run(k);
        };
        auto assign = [&](Id fleet, int div) { Command k; k.type = Cmd::AssignDivision; k.a = fleet; k.b = div; return run(k); };
        // espionage needs a director
        Id rival = NONE;
        for (auto& oc : w.corps) if (oc.alive && !oc.player && oc.arch == Arch::Industrialist) { rival = oc.id; break; }
        c = Command{}; c.type = Cmd::StartSpyOp; c.a = (int)SpyKind::Dossier; c.b = rival;
        r = run(c);
        check("espionage needs an Intelligence Director", !r.ok, r.msg);
        // mining
        Id md = hireExec(SEAT_MINING);
        check("hire a Mining Director", md != NONE);
        Id q1 = w.spawnShip(w.player, QUARRIER, home), q2 = w.spawnShip(w.player, QUARRIER, home);
        Id mf = w.createFleet(w.player, {q1, q2}, "Mining Wing");
        r = assign(mf, DIV_MINING);
        check("assign a fleet to the Mining division", r.ok && w.fleets[mf].division == DIV_MINING, r.msg);
        DivPolicy mp; mp.zones = 1; mp.maxRisk = 45; mp.radius = 4;
        r = automate(DIV_MINING, mp);
        check("hand the division to its director", r.ok, r.msg);
        w.advance(120);
        check("the director sends it mining", w.fleets[mf].order.kind == OrderKind::Mine, w.pc().divs[DIV_MINING].status);
        // trade
        Id td = hireExec(SEAT_TRADE);
        Id cr1 = w.spawnShip(w.player, CARRACK, home);
        Id tf = w.createFleet(w.player, {cr1}, "Trade Wing");
        assign(tf, DIV_TRADE);
        DivPolicy tp; tp.zones = 3; tp.maxRisk = 60; tp.radius = 8; tp.budget = 40000000 * CR;
        automate(DIV_TRADE, tp);
        // security
        Id sd = hireExec(SEAT_SECURITY);
        Id l1 = w.spawnShip(w.player, LANCER, home), l2 = w.spawnShip(w.player, LANCER, home);
        Id sf = w.createFleet(w.player, {l1, l2}, "Escort Wing");
        r = assign(sf, DIV_SECURITY);
        DivPolicy sp; sp.zones = 3; sp.maxRisk = 80; sp.radius = 6;
        automate(DIV_SECURITY, sp);
        // industry: a Hull Plating original and the minerals for it
        Id idr = hireExec(SEAT_INDUSTRY);
        w.addItem(w.player, home, blueprint(BP_COMPONENT_FIRST).bpo, 1);
        for (auto& [m, qn] : blueprint(BP_COMPONENT_FIRST).materials) w.addItem(w.player, home, m, qn * 6);
        DivPolicy ip; ip.product = BP_COMPONENT_FIRST; ip.lines = 2; ip.research = false; ip.budget = 5000000 * CR;
        automate(DIV_INDUSTRY, ip);
        // haulage: stock left at a neighbouring station comes home
        Id hd = hireExec(SEAT_LOGISTICS);
        Id dr = w.spawnShip(w.player, DROVER, home);
        Id hf = w.createFleet(w.player, {dr}, "Freight Wing");
        assign(hf, DIV_HAULAGE);
        Id away = NONE;
        for (Id sy : w.systemsWithin(w.stations[home].system, 3))  // nearest first: the home system if it has another station
            for (Id st : w.systems[sy].stations)
                if (away == NONE && st != home && w.stationUsable(st) && !w.stations[st].isOutpost() && w.systems[sy].zone == Zone::Charter) away = st;
        for (Id sy : w.systemsWithin(w.stations[away].system, 0)) w.systems[sy].heat = 0;  // calm, for the test
        w.addItem(w.player, away, COMPONENT_FIRST + 1, 1500);
        DivPolicy hp; hp.zones = 1; hp.maxRisk = 45; hp.radius = 6; hp.budget = 5000000 * CR;
        automate(DIV_HAULAGE, hp);
        w.advance(150);
        check("four more directors hired", td != NONE && sd != NONE && idr != NONE && hd != NONE);
        check("the Trade Director finds a run", w.fleets[tf].order.kind == OrderKind::Trade || w.fleets[tf].order.kind == OrderKind::Move,
              w.pc().divs[DIV_TRADE].status);
        check("Security patrols where the miners work", w.fleets[sf].order.kind == OrderKind::Patrol, w.pc().divs[DIV_SECURITY].status);
        bool building = false;
        for (auto& j : w.jobs) building = building || (j.corp == w.player && j.division == DIV_INDUSTRY && j.kind == JobKind::Manufacture);
        check("the Industry Director starts the lines", building, w.pc().divs[DIV_INDUSTRY].status);
        int64_t ore0 = w.pstats.oreUnloaded;
        w.advance(3 * MINUTES_PER_DAY);
        if (!w.fleets[hf].alive)
            for (auto& e : w.log)
                if (e.text.find("Freight Wing") != std::string::npos || (e.text.find("lost Drover") != std::string::npos && e.mine))
                    std::printf("    %s %s\n", fmtDate(e.t).c_str(), e.text.c_str());
        check("Haulage brought the stock home", w.have(w.player, home, COMPONENT_FIRST + 1) >= 1500 && w.have(w.player, away, COMPONENT_FIRST + 1) == 0,
              w.pc().divs[DIV_HAULAGE].status);
        Money mrev = w.pc().divs[DIV_MINING].revenue;
        if (!w.pc().divs[DIV_MINING].reports.empty()) mrev += w.pc().divs[DIV_MINING].reports.back().revenue;
        check("mining books the ore it brings in", mrev > 0 && w.pstats.oreUnloaded > ore0,
              fmtMoneyShort(mrev) + ", " + fmtInt(w.pstats.oreUnloaded - ore0) + " ore; " + w.fleets[mf].status);
        // a direct order takes a fleet back
        Id back = NONE;
        for (Id x : {tf, hf, mf}) if (back == NONE && w.fleets[x].alive) back = x;
        c = Command{}; c.type = Cmd::SetFleetOrder; c.a = back; c.order.kind = OrderKind::Move; c.order.station = home;
        r = run(c);
        check("a direct order takes the fleet back", r.ok && w.fleets[back].division == -1, r.msg);
        // the month's reports
        int day = (int)(w.now / MINUTES_PER_DAY);
        w.advance((30 - day % 30) * MINUTES_PER_DAY + 10);
        check("monthly division reports", !w.pc().divs[DIV_MINING].reports.empty(),
              w.pc().divs[DIV_MINING].reports.empty() ? "" : w.pc().divs[DIV_MINING].reports.back().note);
        // a rival bids for the Mining Director; match it
        StaffRequest pr;
        pr.id = (Id)w.staffReqs.size(); pr.kind = StaffReqKind::Poach; pr.person = md; pr.rival = rival;
        pr.wage = w.people[md].wage * 3 / 2; pr.expires = w.now + 3 * MINUTES_PER_DAY;
        w.staffReqs.push_back(pr);
        c = Command{}; c.type = Cmd::AnswerStaff; c.a = pr.id; c.flag = true;
        r = run(c);
        check("match a rival's bid for your director", r.ok && w.people[md].wage == pr.wage && w.pc().exec[SEAT_MINING] == md, r.msg);
        // and let one go
        StaffRequest pr2 = pr;
        pr2.id = (Id)w.staffReqs.size(); pr2.person = sd; pr2.wage = w.people[sd].wage * 2;
        w.staffReqs.push_back(pr2);
        c.a = pr2.id; c.flag = false;
        run(c);
        w.advance(MINUTES_PER_DAY);
        check("a director you do not keep is poached", w.pc().exec[SEAT_SECURITY] == NONE && w.people[sd].employer == rival);
        // espionage
        Id id2 = hireExec(SEAT_INTEL);
        w.people[id2].mgmt = 95;
        c = Command{}; c.type = Cmd::StartSpyOp; c.a = (int)SpyKind::Dossier; c.b = rival;
        before = p.wallet;
        r = run(c);
        check("open a dossier on a rival", r.ok && p.wallet == before - w.spyCost(rival, SpyKind::Dossier), r.msg);
        Id dossierOp = r.id;
        w.advance(3 * MINUTES_PER_DAY);
        const SpyOp& op = w.spyOps[dossierOp];
        check("the operation reports back", !op.active && op.outcome != 0, op.note);
        w.corps[rival].me[BP_SHIP_FIRST + 1] = 9;
        c = Command{}; c.type = Cmd::StartSpyOp; c.a = (int)SpyKind::Research; c.b = rival; c.item = BP_SHIP_FIRST + 1;
        r = run(c);
        check("send agents after a rival's research", r.ok, r.msg);
        Id opId = r.id;
        w.advance(7 * MINUTES_PER_DAY);
        check("research theft resolves", opId >= 0 && !w.spyOps[opId].active && w.spyOps[opId].outcome != 0, w.spyOps[opId].note);
    }
    // ---- construction: outposts in Contract space, upgrades, megaprojects
    {
        Id homeSys = w.stations[home].system;
        Id empty = NONE;
        for (Id sy : w.systemsWithin(homeSys, 10)) {
            const StarSystem& ss = w.systems[sy];
            if (ss.zone == Zone::Contract && ss.owner == NONE && ss.stations.empty()) { empty = sy; break; }
        }
        Id kitShip = w.spawnShip(w.player, CARRACK, home);
        Id kf = w.createFleet(w.player, {kitShip}, "Kit Carrier");
        w.fleets[kf].cargo[OUTPOST_KIT] = 1;
        w.fleets[kf].docked = NONE;
        w.fleets[kf].system = empty;
        c = Command{}; c.type = Cmd::AnchorOutpost; c.a = kf;
        r = run(c);
        check("anchor an outpost in an empty Contract system", r.ok && empty != NONE, r.msg);
        Id op2 = r.id;
        w.addItem(w.player, op2, FUEL_CELLS, 3000);
        w.advance(25 * MINUTES_PER_HOUR);
        double y0 = w.refineYield(w.player, op2);
        for (auto& [it, q] : w.projectNeeds(ProjectKind::Upgrade, SMOD_REFINERY, 1)) w.addItem(w.player, op2, it, q);
        c = Command{}; c.type = Cmd::UpgradeStation; c.a = op2; c.b = SMOD_REFINERY;
        r = run(c);
        Id up = r.id;
        check("upgrade its refinery: materials and credits in", r.ok && up != NONE && w.projects[up].stage == ProjectStage::Building, r.msg);
        w.advance(3 * MINUTES_PER_DAY);
        check("refinery tier 1: better yield", w.stations[op2].smod[SMOD_REFINERY] == 1 && w.refineYield(w.player, op2) > y0,
              std::to_string(y0) + " -> " + std::to_string(w.refineYield(w.player, op2)));
        // a jump gate from home to a far hub
        Id far = NONE;
        for (Id from : w.systemsWithin(homeSys, 3)) {
            for (Id h : w.hubs) {
                Id sy = w.stations[h].system;
                if (w.projectWhy(w.player, ProjectKind::JumpGate, from, sy, NONE).empty()) { far = sy; break; }
            }
            if (far != NONE) { homeSys = from; break; }
        }
        int j0 = w.jumps(homeSys, far);
        c = Command{}; c.type = Cmd::FoundProject; c.a = (int)ProjectKind::JumpGate; c.b = homeSys; c.qty = far;
        r = run(c);
        Id gp = r.id;
        check("found a jump gate", r.ok && gp != NONE, r.msg);
        if (gp != NONE) {
            Project& pg = w.projects[gp];
            for (auto& [it, q] : pg.need) w.addItem(w.player, pg.yard, it, q);
            c = Command{}; c.type = Cmd::ContributeProject; c.a = gp; c.price = pg.cash; c.flag = true;
            r = run(c);
            check("fund it: materials delivered, credits paid", r.ok && w.projects[gp].stage == ProjectStage::Building, r.msg);
            w.advance(21 * MINUTES_PER_DAY);
            check("the gate opens a new lane", w.isGate(homeSys, far) && w.jumps(homeSys, far) == 1,
                  std::to_string(j0) + " jumps -> " + std::to_string(w.jumps(homeSys, far)));
            std::vector<Id> rt = w.route(homeSys, far, RoutePolicy::Shortest);
            check("routes use it", rt.size() == 1, std::to_string(rt.size()) + " jumps");
            Id other = NONE;
            for (auto& oc : w.corps) if (oc.alive && !oc.player && oc.arch == Arch::Trader && !w.projects[gp].share.count(oc.id)) { other = oc.id; break; }
            Id osh = w.spawnShip(other, DROVER, home);
            Id of = w.createFleet(other, {osh}, "Toll payer");
            before = p.wallet;
            w.payToll(w.fleets[of], homeSys, far);
            check("others pay the toll to its backers", p.wallet > before, fmtMoneyShort(p.wallet - before));
        }
        // a Charter Beacon over a Contract system
        Id bsys = NONE;
        for (Id sy : w.systemsWithin(homeSys, 8))
            if (w.projectWhy(w.player, ProjectKind::Beacon, sy, NONE, NONE).empty()) { bsys = sy; break; }
        c = Command{}; c.type = Cmd::FoundProject; c.a = (int)ProjectKind::Beacon; c.b = bsys;
        r = run(c);
        Id bp = r.id;
        check("found a Charter Beacon (the Charter pays 40%)", r.ok && w.projects[bp].cashGot >= w.projects[bp].cash * 40 / 100, r.msg);
        for (auto& [it, q] : w.projects[bp].need) w.addItem(w.player, w.projects[bp].yard, it, q);
        c = Command{}; c.type = Cmd::ContributeProject; c.a = bp; c.price = w.projects[bp].cash; c.flag = true;
        run(c);
        w.advance(13 * MINUTES_PER_DAY);
        check("the Wardens now answer in that Contract system", w.systems[bsys].beacon == 2 && w.wardenRound(bsys) >= 0,
              "round " + std::to_string(w.wardenRound(bsys)));
    }
    // ---- planets and colonies
    {
        Id homeSys = w.stations[home].system;
        Id csys = NONE;
        int cpl = -1;
        float bestHab = 0;
        for (Id sy : w.systemsWithin(homeSys, 8)) {
            if (w.systems[sy].zone != Zone::Contract) continue;
            for (auto& pl : w.planetsOf(sy))
                if (!w.colonyOn(sy, pl.index) && pl.hab * pl.size > bestHab) { bestHab = pl.hab * pl.size; csys = sy; cpl = pl.index; }
        }
        std::vector<Planet> a1 = w.planetsOf(csys), a2 = w.planetsOf(csys);
        check("planets are the same every time they are asked for", a1.size() == a2.size() && a1[cpl].hab == a2[cpl].hab,
              std::to_string(a1.size()) + " planets, best " + a1[cpl].name + " (" + planetTypeName(a1[cpl].type) + ")");
        Id cs = w.spawnShip(w.player, CARRACK, home);
        Id cf = w.createFleet(w.player, {cs}, "Ark");
        w.fleets[cf].cargo[COLONY_KIT] = 1;
        w.fleets[cf].docked = NONE;
        w.fleets[cf].system = csys;
        c = Command{}; c.type = Cmd::SettleColony; c.a = cf; c.b = cpl;
        r = run(c);
        Id col = r.id;
        check("settle a planet with a Colony Kit", r.ok && col != NONE, r.msg);
        if (col != NONE) {
            Id cst = w.colonies[col].station;
            check("the colony is a market", w.bestBid(cst, RATIONS) > 0, "rations bid " + fmtMoney(w.bestBid(cst, RATIONS)));
            Money tax0 = w.colonies[col].taxes;
            w.advance(3 * MINUTES_PER_DAY);
            int64_t out = 0;
            for (auto& [it, sh] : planetOutputs(w.colonies[col].type)) out += w.have(w.player, cst, it);
            check("it pays taxes and hands over its output", w.colonies[col].taxes > tax0 && out > 0,
                  fmtMoneyShort(w.colonies[col].taxes) + " taxes, " + fmtInt(out) + " units");
            float before = w.stations[cst].stock[RATIONS];
            w.addItem(w.player, cst, RATIONS, 2000);
            c = Command{}; c.type = Cmd::SupplyColony; c.a = col;
            r = run(c);
            check("supply it from your hangar", r.ok && w.stations[cst].stock[RATIONS] > before, r.msg);
            double cap0 = w.colonies[col].cap();
            for (auto& [it, q] : w.projectNeeds(ProjectKind::ColonyUpgrade, CMOD_HABITAT, 1)) w.addItem(w.player, cst, it, q);
            c = Command{}; c.type = Cmd::UpgradeColony; c.a = col; c.b = CMOD_HABITAT;
            r = run(c);
            check("build more habitats", r.ok, r.msg);
            w.advance(4 * MINUTES_PER_DAY);
            check("room for more people", w.colonies[col].cmod[CMOD_HABITAT] == 1 && w.colonies[col].cap() > cap0,
                  fmtQty(cap0) + "k -> " + fmtQty(w.colonies[col].cap()) + "k");
            w.advance(20 * MINUTES_PER_DAY);
            check("a supplied colony grows", w.colonies[col].pop > 5.0, fmtQty(w.colonies[col].pop) + "k people, needs met " +
                  std::to_string((int)(w.colonies[col].sat * 100)) + "%");
        }
    }
    // ---- capital markets
    {
        p.wallet += 600000000 * CR;
        p.netWorth = w.computeNetWorth(w.player);
        Id hub = w.hubs[0];
        int fit = MINERAL_FIRST + 2;
        c = Command{}; c.type = Cmd::OpenFuture; c.a = hub; c.item = fit; c.qty = 200000; c.flag = true; c.b = 7;
        r = run(c);
        check("buy a 7-day futures contract", r.ok && !w.futures.empty() && w.futures.back().open, r.msg);
        Id fid = w.futures.empty() ? NONE : w.futures.back().id;
        c = Command{}; c.type = Cmd::OpenFuture; c.a = hub; c.item = fit; c.qty = 1000; c.flag = false; c.b = 14;
        r = run(c);
        Id fid2 = w.futures.back().id;
        c = Command{}; c.type = Cmd::CloseFuture; c.a = fid2;
        Result r2 = run(c);
        check("sell one short and close it early", r.ok && r2.ok && r2.value < 0, r2.msg);
        Tick settleAt = (w.futures[w.futures.size() - 2].expiry + MINUTES_PER_DAY - 1) / MINUTES_PER_DAY * MINUTES_PER_DAY;
        w.advance(settleAt - w.now - 30);
        Money spot0 = w.refPrice(hub, fit);
        w.stations[hub].stock[fit] = 0;  // a squeeze: you buy the hub dry just before expiry
        w.advance(60);
        const Future* f = nullptr;
        for (auto& x : w.futures) if (x.id == fid) f = &x;
        check("it settles in cash against the hub price", f && !f->open && f->result > 0,
              f ? fmtMoney(f->result) + " (spot " + fmtMoney(spot0) + " -> " + fmtMoney(w.refPrice(hub, fit)) + ")" : "missing");
        // short selling
        Id tgt = NONE;  // the richest-priced rival against its book value: the one most likely to fall
        double rich = 0;
        for (auto& oc : w.corps) {
            if (!oc.alive || oc.player || w.lendableShares(oc.id) <= 30000 || oc.netWorth <= 0) continue;
            double ratio = (double)oc.sharePrice * oc.sharesOut / (double)oc.netWorth;
            if (ratio > rich) { rich = ratio; tgt = oc.id; }
        }
        c = Command{}; c.type = Cmd::OpenShort; c.a = tgt; c.qty = 20000;
        r = run(c);
        check("borrow and sell a rival's shares", r.ok && !w.shorts.empty() && w.shorts.back().open, r.msg);
        Id sid = w.shorts.back().id;
        Money px0 = w.corps[tgt].sharePrice;
        w.corps[tgt].smear = 0.4f;  // a smear campaign does the rest
        w.advance(6 * MINUTES_PER_DAY);
        c = Command{}; c.type = Cmd::CloseShort; c.a = sid;
        r = run(c);
        check("cover the short after the price falls", r.ok && r.value > 0,
              r.msg + " (" + fmtMoney(px0) + " -> " + fmtMoney(w.corps[tgt].sharePrice) + ")");
        // bonds
        Money before = p.wallet;
        c = Command{}; c.type = Cmd::IssueBond; c.qty = 25; c.b = 30;
        r = run(c);
        Id bid = w.bonds.empty() ? NONE : w.bonds.back().id;
        check("issue 25M cr of 30-day bonds", r.ok && p.wallet - before == 25 * BOND_UNIT - 25 * BOND_UNIT / 100, r.msg);
        Id aib = NONE;
        for (int d = 0; d < 40 && aib == NONE; d++) {
            for (auto& b : w.bonds)
                if (!b.done && b.issuer != w.player && b.onOffer > 0 && b.offerEnds > w.now) { aib = b.id; break; }
            if (aib == NONE) w.advance(MINUTES_PER_DAY);
        }
        if (aib != NONE) {
            c = Command{}; c.type = Cmd::BuyBond; c.a = aib; c.qty = 2;
            r = run(c);
            const Bond* ab = nullptr;
            for (auto& b : w.bonds) if (b.id == aib) ab = &b;
            check("buy a rival's bonds from the offer", r.ok && ab && ab->heldBy(w.player) == 2, r.msg);
            c = Command{}; c.type = Cmd::SellBond; c.a = aib; c.qty = 1;
            r = run(c);
            check("sell one back to the market", r.ok && ab->heldBy(w.player) == 1, r.msg);
        } else {
            check("a rival puts bonds on offer", false, "none in 40 days");
        }
        for (int d = 0; d < 35; d++) {
            const Bond* mb = nullptr;
            for (auto& b : w.bonds) if (b.id == bid) mb = &b;
            if (!mb || mb->done) break;
            w.advance(MINUTES_PER_DAY);
        }
        const Bond* mb = nullptr;
        for (auto& b : w.bonds) if (b.id == bid) mb = &b;
        check("coupon and principal paid at maturity", mb && mb->done && !mb->defaulted && mb->coupons == 1,
              mb ? "coupon " + fmtMoney(mb->coupon()) : "missing");
        // a poison pill in an AI corporation
        Id small = NONE;
        Money cap = 0;
        for (auto& oc : w.corps) {
            if (!oc.alive || oc.player || oc.id == tgt || p.shares.count(oc.id) || w.corpHeldShares(oc.id) > 0) continue;
            Money mc = oc.sharePrice * oc.sharesOut;
            if (small == NONE || mc < cap) { small = oc.id; cap = mc; }
        }
        Corp& sc = w.corps[small];
        p.wallet += cap * 2;
        r = w.tradeShares(w.player, small, sc.sharesOut * 16 / 100, true);
        for (int d = 0; d < 30 && !sc.poisonPill; d++) w.advance(MINUTES_PER_DAY);
        check("a rival adopts a poison pill against your stake", sc.poisonPill, sc.name);
        int64_t s0 = sc.sharesOut;
        r = w.tradeShares(w.player, small, sc.sharesOut * 10 / 100, true);
        double pct = 100.0 * p.shares[small] / sc.sharesOut;
        check("crossing 25% sets it off and dilutes you", sc.sharesOut == 2 * s0 && pct < 14.0, r.msg);
        // a raider in you
        Id raider = NONE;
        for (auto& oc : w.corps)
            if (oc.alive && !oc.player && oc.id != small && oc.id != tgt && oc.arch != Arch::Miner) { raider = oc.id; break; }
        w.corps[raider].wallet += p.sharePrice * p.sharesOut;
        p.raider = raider;
        p.raidDeadline = 0;
        for (int d = 0; d < 30 && p.raidDeadline == 0; d++) w.advance(MINUTES_PER_DAY);
        int64_t stake = w.corps[raider].shares.count(w.player) ? w.corps[raider].shares[w.player] : 0;
        check("a raider builds a stake and wants greenmail", p.raidDeadline > 0 && stake * 10 >= p.sharesOut,
              w.corps[raider].name + " holds " + fmtInt(stake));
        int64_t out0 = p.sharesOut;
        c = Command{}; c.type = Cmd::PayGreenmail;
        r = run(c);
        check("pay it off: its shares are cancelled", r.ok && p.raider == NONE && p.sharesOut == out0 - stake, r.msg);
        c = Command{}; c.type = Cmd::AdoptPill;
        r = run(c);
        check("adopt a poison pill of your own", r.ok && p.poisonPill, r.msg);
        Id raider2 = NONE;
        for (auto& oc : w.corps)
            if (oc.alive && !oc.player && oc.id != raider && oc.id != small && oc.id != tgt) { raider2 = oc.id; break; }
        w.corps[raider2].wallet += p.sharePrice * p.sharesOut;
        p.publicFloat = p.sharesOut * 45 / 100;  // as after a secondary offering: enough float to pass 25%
        p.raider = raider2;
        r = w.tradeShares(raider2, w.player, p.sharesOut * 26 / 100, true);
        int64_t st2 = w.corps[raider2].shares[w.player];
        check("a raider passing 25% is diluted and backs off", !p.poisonPill && p.raider == NONE && st2 * 100 / p.sharesOut < 15,
              std::to_string(st2 * 100 / p.sharesOut) + "% left");
        // your own underwriting desk
        c = Command{}; c.type = Cmd::SetUnderwriting;
        c.uw.open = true; c.uw.rateMul = 1.0f; c.uw.maxRisk = 60; c.uw.capacity = 300000000 * CR;
        r = run(c);
        check("open an underwriting desk", r.ok && w.uw.open && w.uw.licensed, r.msg);
        w.advance(8 * MINUTES_PER_DAY);
        check("rivals buy cover from you", w.uw.policies > 0 && w.uw.premiums > 0,
              std::to_string(w.uw.policies) + " policies, " + fmtMoneyShort(w.uw.premiums) + " premiums");
        Id insured = w.uw.book.empty() ? NONE : w.uw.book.begin()->first;
        Money owed = insured == NONE ? 0 : w.uw.book.begin()->second.first;
        Money claims0 = w.uw.claims;
        if (insured != NONE) w.destroyShip(insured, NONE, nullptr);
        check("a loss on your book is a claim you pay", insured != NONE && w.uw.claims == claims0 + owed, fmtMoney(owed));
    }
    w.advance(MINUTES_PER_DAY);
    std::string err;
    bool ok = w.save("features.n1save", &err);
    World w2;
    ok = ok && w2.load("features.n1save", &err);
    if (ok) {
        for (int m = 0; m < 300; m++) { w.tick(); w2.tick(); }
        ok = w.pc().wallet == w2.pc().wallet && w.rng.s[0] == w2.rng.s[0];
    }
    std::remove("features.n1save");
    check("save, reload and replay identically", ok, err);
    std::printf("feature checks: %d failed\n", fails);
    return fails;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--features") == 0)
            return featureTest(i + 1 < argc ? std::strtoull(argv[i + 1], nullptr, 10) : 7) ? 1 : 0;
    int days = 60;
    uint64_t seed = 7;
    bool play = false, quiet = false;
    GalaxySize size = GalaxySize::Compact;
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--play") == 0) play = true;
        else if (std::strcmp(argv[i], "--quiet") == 0) quiet = true;
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            std::string s = argv[++i];
            size = s == "standard" ? GalaxySize::Standard : s == "vast" ? GalaxySize::Vast : s == "galaxy" ? GalaxySize::Galaxy : s == "expanse" ? GalaxySize::Expanse : GalaxySize::Compact;
        } else if (pos == 0) { days = std::atoi(argv[i]); pos++; }
        else if (pos == 1) { seed = std::strtoull(argv[i], nullptr, 10); pos++; }
    }

    double mem0 = rssMB();
    World w;
    NewGameSetup s;
    s.seed = seed;
    s.size = size;
    auto t0 = std::chrono::steady_clock::now();
    w.generate(s);
    auto t1 = std::chrono::steady_clock::now();
    int zones[ZONE_COUNT] = {};
    for (auto& sy : w.systems) zones[(int)sy.zone]++;
    int stTypes[STATION_TYPE_COUNT] = {};
    for (auto& st : w.stations) stTypes[(int)st.type]++;
    size_t lanes = 0;
    for (auto& sy : w.systems) lanes += sy.lanes.size();
    std::printf("NebulaOne selftest  %s chart  seed %llu  days %d%s\n", galaxySizeName(size), (unsigned long long)seed, days,
                play ? "  (scripted player)" : "");
    std::printf("chart: %zu hex cells (radius %d), %zu systems (Charter %d, Contract %d, Frontier %d), %zu lanes, %zu regions\n",
                w.cells.size(), w.hexRadius, w.systems.size(), zones[0], zones[1], zones[2], lanes / 2, w.regions.size());
    std::printf("       %zu stations (%zu hubs, %d outposts), %zu corporations, %zu ships; generated in %.0f ms, %.0f MB\n",
                w.stations.size(), w.hubs.size(), stTypes[(int)StationType::Outpost], w.corps.size(), w.ships.size(),
                std::chrono::duration<double, std::milli>(t1 - t0).count(), rssMB() - mem0);
    std::printf("station types: hub %d ind %d agri %d colony %d research %d military %d outpost %d\n", stTypes[0], stTypes[1],
                stTypes[2], stTypes[3], stTypes[4], stTypes[5], stTypes[6]);
    {
        // connectivity and diameter estimate from a handful of sources
        int maxJ = 0, unreachable = 0;
        for (int k = 0; k < 6; k++) {
            Id src = (Id)((k * 7919) % w.systems.size());
            const auto& row = w.jumpsFrom(src);
            for (auto h : row) { if (h == 999) unreachable++; else maxJ = std::max<int>(maxJ, h); }
        }
        std::printf("lanes: all systems connected: %s; longest route seen %d jumps\n", unreachable ? "NO" : "yes", maxJ);
        auto r0 = std::chrono::steady_clock::now();
        int routes = 0;
        size_t hops = 0;
        for (int k = 0; k < 200; k++) {
            Id a = (Id)w.rng.irange(0, (int)w.systems.size() - 1), b = (Id)w.rng.irange(0, (int)w.systems.size() - 1);
            hops += w.route(a, b, k % 2 ? RoutePolicy::LowestRisk : RoutePolicy::Shortest).size();
            routes++;
        }
        auto r1 = std::chrono::steady_clock::now();
        std::printf("routing: %d random A* routes, mean %.1f jumps, %.3f ms each\n", routes, (double)hops / routes,
                    std::chrono::duration<double, std::milli>(r1 - r0).count() / routes);
    }
    std::vector<Money> nw0;
    for (auto& c : w.corps) nw0.push_back(c.netWorth);

    if (!quiet) std::printf("\n day  goods  minerals  ore  comps  ships | rations lo-hi  ferrox lo-hi | battles  lost  live  player net worth\n");
    auto t2 = std::chrono::steady_clock::now();
    double worstDay = 0;
    for (int d = 0; d < days; d++) {
        auto d0 = std::chrono::steady_clock::now();
        for (int m = 0; m < MINUTES_PER_DAY; m++) {
            w.tick();
            if (play) playerScript(w, d);
        }
        worstDay = std::max(worstDay, std::chrono::duration<double>(std::chrono::steady_clock::now() - d0).count());
        if (!quiet && (d % 5 == 4 || d == days - 1)) {
            int lost = 0, live = 0;
            for (auto& c : w.corps) lost += c.shipsLost;
            for (auto& st : w.stations) live += st.makerLive;
            double rl, rh, fl, fh;
            spread(w, RATIONS, rl, rh);
            spread(w, FERROX, fl, fh);
            std::printf("%4d  %5.2f  %8.2f  %4.2f  %5.2f  %5.2f | %4.2f-%4.2f     %4.2f-%4.2f  | %6zu  %4d  %4d  %s\n", d + 1,
                        priceIndex(w, GOOD_FIRST, COMPONENT_FIRST), priceIndex(w, MINERAL_FIRST, GOOD_FIRST),
                        priceIndex(w, ORE_FIRST, MINERAL_FIRST), priceIndex(w, COMPONENT_FIRST, MODULE_FIRST),
                        priceIndex(w, SHIP_FIRST, STRUCTURE_FIRST), rl, rh, fl, fh, w.battles.size(), lost, live,
                        fmtMoneyShort(w.pc().netWorth).c_str());
        }
    }
    auto t3 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t3 - t2).count();
    std::printf("\nsimulated %d days in %.2f s: %.0f game minutes per real second (slowest day %.0f ms); memory %.0f MB\n", days, secs,
                days * 1440.0 / std::max(1e-9, secs), worstDay * 1000, rssMB() - mem0);
    std::printf("  fastest game speed (6 h per second) needs 360 game minutes per second: %s\n",
                days * 1440.0 / secs >= 360 ? "met" : "NOT met");

    if (!quiet || w.corps.size() <= 20) {
        std::printf("\n%-32s %-13s %12s %12s %12s %6s %5s %5s %9s\n", "corporation", "profile", "net worth 0", "net worth", "wallet",
                    "ships", "lost", "kills", "share px");
        for (auto& c : w.corps) {
            if (w.corps.size() > 20 && !c.player && c.id % (int)(w.corps.size() / 16) != 0) continue;
            int n = 0;
            for (Id sh : c.shipList) n += w.ships[sh].alive && w.ships[sh].corp == c.id;
            std::printf("%-32s %-13s %12s %12s %12s %6d %5d %5d %9s\n", c.name.substr(0, 32).c_str(), archName(c.arch),
                        fmtMoneyShort(nw0[c.id], false).c_str(), fmtMoneyShort(c.netWorth, false).c_str(),
                        fmtMoneyShort(c.wallet, false).c_str(), n, c.shipsLost, c.shipsKilled, fmtMoneyShort(c.sharePrice, false).c_str());
        }
    }
    std::array<double, 7> archGrowth{}, archN{};
    for (auto& c : w.corps) {
        if (c.player || !c.alive) continue;
        archGrowth[(int)c.arch] += (double)c.netWorth / std::max<double>(1, (double)nw0[c.id]);
        archN[(int)c.arch]++;
    }
    std::printf("\nAI net worth multiple by profile:");
    for (int a = 1; a < 7; a++)
        if (archN[a] > 0) std::printf("  %s %.2fx", archName((Arch)a), archGrowth[a] / archN[a]);
    std::printf("\n");
    std::printf("\nAI income by category over the last 30 days (sum across corps):\n");
    std::array<Money, CAT_COUNT> tot{};
    for (auto& c : w.corps) {
        if (c.player) continue;
        int k = 0;
        for (auto it = c.catDays.rbegin(); it != c.catDays.rend() && k < 30; ++it, ++k)
            for (int i = 0; i < CAT_COUNT; i++) tot[i] += (*it)[i];
    }
    for (int i = 0; i < CAT_COUNT; i++)
        if (tot[i]) std::printf("  %-26s %14s\n", catLabel((Cat)i), fmtMoneyShort(tot[i]).c_str());

    double faucet = 0;
    if (!quiet) std::printf("\nstation money flow by item (whole game): paid to corps / received from corps / net faucet\n");
    for (int i = 0; i < ITEM_COUNT; i++)
        if (w.dbgMakerPaid[i] + w.dbgMakerReceived[i] > 0) {
            double net = w.dbgMakerPaid[i] - w.dbgMakerReceived[i];
            faucet += net;
            if (!quiet)
                std::printf("  %-22s %12s %12s %12s\n", item(i).name.c_str(), fmtMoneyShort(credits(w.dbgMakerPaid[i])).c_str(),
                            fmtMoneyShort(credits(w.dbgMakerReceived[i])).c_str(), fmtMoneyShort(credits(net)).c_str());
        }
    std::printf("  net faucet %s  (%s per day, %s per system per day)\n", fmtMoneyShort(credits(faucet)).c_str(),
                fmtMoneyShort(credits(faucet / std::max(1, days))).c_str(),
                fmtMoneyShort(credits(faucet / std::max(1, days) / w.systems.size())).c_str());
    if (!quiet) {
        std::printf("\ntrade runs by item (whole game): trips / total result\n");
        for (int i = 0; i < ITEM_COUNT; i++)
            if (w.dbgTradeTrips[i] > 0)
                std::printf("  %-20s %6.0f trips  %12s  avg %10s\n", item(i).name.c_str(), w.dbgTradeTrips[i],
                            fmtMoneyShort(credits(w.dbgTradeProfit[i])).c_str(),
                            fmtMoneyShort(credits(w.dbgTradeProfit[i] / w.dbgTradeTrips[i])).c_str());
    }
    if (play) {
        std::printf("\nplayer: ore unloaded %lld, refines %lld, sales %lld, trade runs %lld, wallet %s, net worth %s\n",
                    (long long)w.pstats.oreUnloaded, (long long)w.pstats.refines, (long long)w.pstats.sales,
                    (long long)w.pstats.tradeRuns, fmtMoneyShort(w.pc().wallet).c_str(), fmtMoneyShort(w.pc().netWorth).c_str());
        for (Id fid : w.pc().fleetList) {
            const Fleet& f = w.fleets[fid];
            if (f.alive) std::printf("  fleet %-12s %-10s trips %d  status: %s\n", f.name.c_str(), orderKindName(f.order.kind), f.trips, f.status.c_str());
        }
    }
    int open = 0;
    for (auto& r : w.loanReqs) open += r.open;
    int active = 0, defaulted = 0;
    for (auto& l : w.loans) { active += l.status == 0; defaulted += l.status == 2; }
    int cOpen = 0, cTaken = 0, cDone = 0;
    for (auto& c : w.contracts) {
        cOpen += c.state == ContractState::Open;
        cTaken += c.state == ContractState::Accepted;
        cDone += c.state == ContractState::Done;
    }
    int outposts = 0;
    for (Id o : w.outpostIds) outposts += !w.stations[o].dead;
    std::array<int, RISK_GRADES> grades{};
    for (auto& sy : w.systems) grades[riskGradeIndex(sy.risk)]++;
    std::printf("\nloans: %d active, %d defaulted, %d open requests; events %zu; log lines %zu\n", active, defaulted, open,
                w.events.size(), w.log.size());
    std::printf("contracts: %d open, %d in progress, %d completed; outposts standing %d\n", cOpen, cTaken, cDone, outposts);
    std::printf("risk grades:  AAA %d  AA %d  A %d  BBB %d  BB %d  B %d  CCC %d  D %d\n", grades[0], grades[1], grades[2], grades[3],
                grades[4], grades[5], grades[6], grades[7]);
    {
        int kinds[EVENT_KIND_COUNT] = {};
        for (auto& e : w.events) kinds[(int)e.kind]++;
        std::printf("events (last 10 days):");
        for (int k = 0; k < EVENT_KIND_COUNT; k++)
            if (kinds[k]) std::printf("  %s %d", eventKindName((EventKind)k), kinds[k]);
        int wars = 0, warsLive = 0, allies = 0, naps = 0, ended = 0, hired = 0;
        Money destroyed = 0;
        for (auto& t : w.treaties) {
            if (!t.active) { ended++; continue; }
            if (t.kind == Pact::War) { wars++; warsLive += t.live(w.now); hired += t.hiredBy != NONE; destroyed += t.scoreA + t.scoreB; }
            if (t.kind == Pact::Alliance) allies++;
            if (t.kind == Pact::NonAggression) naps++;
        }
        std::printf("\ndiplomacy: %d wars (%d live, %d hired, %s destroyed), %d alliances, %d pacts, %d ended in the last month; %zu offers\n",
                    wars, warsLive, hired, fmtMoneyShort(destroyed).c_str(), allies, naps, ended, w.offers.size());
        for (auto& t : w.treaties) {
            if (!t.active || t.kind != Pact::War) continue;
            int fl = 0;
            for (auto& f : w.fleets)
                if (f.alive && f.order.targetCorp != NONE && (f.order.targetCorp == t.a || f.order.targetCorp == t.b)) fl++;
            std::printf("  war: %s (%s) vs %s (%s)%s  score %s / %s  fleets assigned %d  %s\n", w.corps[t.a].name.c_str(), archName(w.corps[t.a].arch),
                        w.corps[t.b].name.c_str(), archName(w.corps[t.b].arch), t.hiredBy != NONE ? " [hired]" : "", fmtMoneyShort(t.scoreA).c_str(),
                        fmtMoneyShort(t.scoreB).c_str(), fl, t.live(w.now) ? "live" : "pending");
        }
        int found = 0, byPlayer = 0, done = 0;
        int kindsS[4] = {};
        for (auto& st : w.sites) { found++; byPlayer += st.finder == w.player; done += st.done; kindsS[(int)st.kind]++; }
        std::printf("exploration: %d sites on file (%d the player's, %d finished): derelicts %d, relics %d, pockets %d, drift lanes %d\n", found,
                    byPlayer, done, kindsS[0], kindsS[1], kindsS[2], kindsS[3]);
        int seen = 0, sighted = 0;
        for (auto v : w.sight) { seen += v >= 2; sighted += v >= 1; }
        std::printf("intel: fog %s, %d systems in live sight, %d sighted, %zu station snapshots\n", w.fog ? "on" : "off", seen, sighted, w.intel.size());
    }

    std::string path = "selftest.n1save";
    std::string err;
    auto s0 = std::chrono::steady_clock::now();
    bool ok = w.save(path, &err);
    auto s1 = std::chrono::steady_clock::now();
    long fsize = 0;
    if (FILE* fp = std::fopen(path.c_str(), "rb")) { std::fseek(fp, 0, SEEK_END); fsize = std::ftell(fp); std::fclose(fp); }
    World w2;
    bool ok2 = ok && w2.load(path, &err);
    auto s2 = std::chrono::steady_clock::now();
    bool same = ok2 && w2.now == w.now && w2.pc().wallet == w.pc().wallet && w2.ships.size() == w.ships.size();
    if (same) {
        for (int m = 0; m < 600; m++) { w.tick(); w2.tick(); }
        same = w.pc().wallet == w2.pc().wallet && w.rng.s[0] == w2.rng.s[0] && w.nextOrderId == w2.nextOrderId;
    }
    std::printf("save: %.1f MB in %.0f ms, load %.0f ms; round trip: %s%s\n", fsize / 1048576.0,
                std::chrono::duration<double, std::milli>(s1 - s0).count(), std::chrono::duration<double, std::milli>(s2 - s1).count(),
                same ? "OK (identical after 10 more hours)" : "FAILED ", err.c_str());
    std::remove(path.c_str());
    return same ? 0 : 1;
}
