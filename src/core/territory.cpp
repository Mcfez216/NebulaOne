// NebulaOne - territory: outposts anchored in the Frontier, their fuel, taxes
// and sieges. An online outpost gives its owner sovereignty over the system.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

Result World::anchorOutpost(Id corp, Id fleetId) {
    if (fleetId < 0 || fleetId >= (Id)fleets.size()) return Result::fail("Unknown fleet.");
    Fleet& f = fleets[fleetId];
    if (!f.alive || f.corp != corp) return Result::fail("Unknown fleet.");
    if (f.docked != NONE || f.act == Act::Jump || f.battle != NONE) return Result::fail("The fleet must be in space in the target system, not docked, jumping or fighting.");
    StarSystem& s = systems[f.system];
    if (s.zone == Zone::Charter) return Result::fail("Outposts cannot be anchored in Charter space.");
    if (s.owner != NONE) return Result::fail(s.name + " is already held by " + corps[s.owner].name + ".");
    for (Id st : s.stations)
        if (!stations[st].dead) return Result::fail("There is already a station in " + s.name + ": outposts go in empty Contract systems or the Frontier.");
    auto it = f.cargo.find(OUTPOST_KIT);
    if (it == f.cargo.end() || it->second <= 0) return Result::fail("The fleet is not carrying an Outpost Kit.");
    if (--it->second <= 0) f.cargo.erase(it);
    Corp& c = corps[corp];
    Id st = createStation(s.id, StationType::Outpost, s.name + " - " + c.ticker + " Outpost");
    Station& o = stations[st];
    o.owner = corp;
    o.state = OutpostState::Anchoring;
    o.stateUntil = now + 24 * MINUTES_PER_HOUR;
    o.shield = 0.25;
    o.hull = 1.0;
    o.marketTax = 0.05f;
    o.refineTax = 0.05f;
    s.owner = corp;
    outpostIds.push_back(st);
    if (corp == player) pstats.outposts++;
    addLog(Chan::News, Sev::Info, corp == player, s.id, c.name + " is anchoring an outpost in " + s.name + (s.zone == Zone::Frontier ? ", in the Frontier." : ", in Contract space."));
    Result r = Result::good("Anchoring has begun. The outpost comes online in 24 hours; keep fuel in its hangar after that.");
    r.id = st;
    return r;
}

static void outpostOnline(World& w, Station& o) {
    o.state = OutpostState::Online;
    if (o.target[GOOD_FIRST] <= 0) {
        w.setupEconomy(o);
        for (int i = 0; i < ITEM_COUNT; i++) {
            double flow = std::max(o.prod[i], o.cons[i]);
            double days = isOre(i) ? 3.0 : isModule(i) ? 8.0 : 5.0;
            double T = flow * days;
            if (isModule(i) && T > 0) T = std::max(3.0, std::round(T));
            o.target[i] = (float)T;
            o.stock[i] = (float)(T * 0.3);  // a new outpost starts hungry
            if (isModule(i)) o.stock[i] = std::round(o.stock[i]);
        }
    }
    o.lastEconomy = w.now;
    w.markActive(o.system);
}

static void destroyOutpost(World& w, Station& o, Id killer) {
    StarSystem& s = w.systems[o.system];
    Corp& owner = w.corps[o.owner];
    o.dead = true;
    o.state = OutpostState::None;
    s.owner = NONE;
    // everything in the hangars is lost; hulls docked there die with it
    for (auto& c : w.corps) {
        auto& h = c.hangar;
        for (auto it = h.lower_bound(World::hkey(o.id, 0)); it != h.end() && it->first < World::hkey(o.id + 1, 0);) it = h.erase(it);
    }
    for (auto& sh : w.ships)
        if (sh.alive && sh.fleet == NONE && sh.station == o.id) w.destroyShip(sh.id, killer, nullptr);
    for (auto& f : w.fleets)
        if (f.alive && f.docked == o.id) { f.docked = NONE; f.act = Act::Idle; f.status = "Home station destroyed"; }
    for (auto& bk : o.books) { bk.sells.clear(); bk.buys.clear(); }
    o.makerLive = false;
    s.losses += (float)toCr(item(OUTPOST_KIT).base) / 1e6f;
    s.heat += 3.0f;
    if (killer != NONE)
        if (Treaty* war = w.findTreaty(Pact::War, killer, o.owner)) (war->a == killer ? war->scoreA : war->scoreB) += item(OUTPOST_KIT).base;
    w.addLog(Chan::News, Sev::Bad, o.owner == w.player || killer == w.player, s.id,
             owner.name + "'s outpost in " + s.name + " has been destroyed" + (killer != NONE ? " by " + w.corps[killer].name : "") + ".");
}

void World::outpostsHourly() {
    for (Id sid : outpostIds) {
        Station& o = stations[sid];
        if (o.dead) continue;
        if (o.state == OutpostState::Anchoring && now >= o.stateUntil) {
            outpostOnline(*this, o);
            o.shield = 1.0;
            addLog(Chan::Corp, Sev::Good, o.owner == player, o.system, o.name + " is online. Market, refinery and factory are open.");
        }
        if (o.state == OutpostState::Reinforced && now >= o.stateUntil) {
            o.state = OutpostState::Online;
            addLog(Chan::Combat, Sev::Warn, o.owner == player, o.system, o.name + " leaves reinforcement: its hull is exposed.");
        }
        // siege damage from fleets ordered to besiege it
        double dmg = 0;
        Id attacker = NONE;
        for (Id fid : fleetsIn[o.system]) {
            const Fleet& f = fleets[fid];
            if (!f.alive || f.docked != NONE || f.battle != NONE || f.system != o.system) continue;
            if (f.order.kind != OrderKind::Siege || f.order.station != sid || f.corp == o.owner) continue;
            dmg += fleetStrength(f) * 30.0;
            attacker = f.corp;
        }
        if (dmg > 0 && o.state != OutpostState::Reinforced && o.state != OutpostState::None) {
            if (o.besieger != attacker || now - o.lastSiege > 6 * MINUTES_PER_HOUR) {
                addLog(Chan::Combat, Sev::Bad, o.owner == player || attacker == player, o.system,
                       corps[attacker].name + " is besieging " + o.name + ".");
                corps[o.owner].standing[attacker] = -10.0f;
            }
            o.besieger = attacker;
            o.lastSiege = now;
            // the Defence Grid's batteries pick off a besieging ship now and then
            if (o.smod[SMOD_DEFENCE] > 0 && o.state == OutpostState::Online && rng.chance(0.15 * o.smod[SMOD_DEFENCE]))
                for (Id fid : fleetsIn[o.system]) {
                    Fleet& f = fleets[fid];
                    if (!f.alive || f.order.kind != OrderKind::Siege || f.order.station != sid || f.ships.empty()) continue;
                    destroyShip(f.ships[rng.irange(0, (int)f.ships.size() - 1)], o.owner, nullptr);
                    addLog(Chan::Combat, Sev::Good, o.owner == player, o.system, o.name + "'s Defence Grid destroyed a besieging ship.");
                    break;
                }
            noteAggression(o.owner, attacker);
            systems[o.system].heat += 0.3f;
            if (o.shield > 0) {
                o.shield -= dmg / OUTPOST_SHIELD / (1.0 + 0.6 * o.smod[SMOD_DEFENCE]);
                if (o.shield <= 0) {
                    o.shield = 0;
                    o.state = OutpostState::Reinforced;
                    o.stateUntil = now + 24 * MINUTES_PER_HOUR;
                    addLog(Chan::Combat, Sev::Bad, o.owner == player || attacker == player, o.system,
                           o.name + "'s shield is down. It is reinforced for 24 hours: the owner has until " + fmtDate(o.stateUntil) + " to defend it.");
                }
            } else {
                o.hull -= dmg / OUTPOST_HULL / (1.0 + 0.6 * o.smod[SMOD_DEFENCE]);
                if (o.hull <= 0) { o.hull = 0; destroyOutpost(*this, o, attacker); continue; }
            }
        } else if (o.state == OutpostState::Online && now - o.lastSiege > 2 * MINUTES_PER_HOUR) {
            o.shield = std::min(1.0, o.shield + 0.05);
            if (o.shield >= 1.0) o.besieger = NONE;
        }
    }
}

void World::outpostsDaily() {
    for (Id sid : outpostIds) {
        Station& o = stations[sid];
        if (o.dead || o.state == OutpostState::Anchoring) continue;
        int64_t fuel = have(o.owner, sid, FUEL_CELLS);
        int64_t burn = OUTPOST_FUEL_PER_DAY * (5 - o.smod[SMOD_SILO]) / 5;
        if (fuel >= burn) {
            takeItem(o.owner, sid, FUEL_CELLS, burn);
            if (o.state == OutpostState::Offline) {
                outpostOnline(*this, o);
                addLog(Chan::Corp, Sev::Good, o.owner == player, o.system, o.name + " is fuelled and back online.");
            }
            int64_t left = fuel - burn;
            if (o.owner == player && left < burn * 3)
                addLog(Chan::Corp, Sev::Warn, true, o.system, o.name + " has " + std::to_string(left / std::max<int64_t>(1, burn)) + " days of Fuel Cells left.");
        } else if (o.state == OutpostState::Online) {
            o.state = OutpostState::Offline;
            for (auto& bk : o.books) {
                auto isMaker = [](const Order& x) { return x.corp == NONE; };
                bk.sells.erase(std::remove_if(bk.sells.begin(), bk.sells.end(), isMaker), bk.sells.end());
                bk.buys.erase(std::remove_if(bk.buys.begin(), bk.buys.end(), isMaker), bk.buys.end());
            }
            o.makerLive = false;
            addLog(Chan::Corp, Sev::Bad, o.owner == player, o.system,
                   o.name + " is out of Fuel Cells and has gone offline: no market, refinery or shield regeneration.");
        }
    }
}

void World::siegeTick(Fleet&) {}

}  // namespace nb
