// NebulaOne - fog of war and intelligence.
//
// With fog on, the player sees live markets and fleets only where its sensors
// reach: its fleets (further with a survey array), its headquarters, its
// outposts, and its allies' fleets and outposts. Everywhere else the market
// window shows the last snapshot it took, with its age. The Charter Exchange
// publishes its hubs' prices at the close of each day, and intel brokers sell
// a region's market report or a day of fleet sightings.
//
// The AI corporations plan from their own neighbourhoods and are not fogged;
// in multiplayer every corporation would carry its own picture.
#include <algorithm>

#include "world.h"

namespace nb {

void World::updateSight() {
    sight.assign(systems.size(), fog ? 0 : 2);
    if (!fog || corps.empty() || player < 0) return;
    auto mark = [&](Id sys, int radius, uint8_t lvl) {
        if (sys == NONE) return;
        if (radius <= 0) { sight[sys] = std::max(sight[sys], lvl); return; }
        for (Id s : systemsWithin(sys, radius)) sight[s] = std::max(sight[s], lvl);
    };
    std::vector<Id> eyes = {player};
    for (auto& c : corps)
        if (c.alive && c.id != player && allied(player, c.id)) eyes.push_back(c.id);
    for (Id corp : eyes) {
        for (Id fid : corps[corp].fleetList) {
            const Fleet& f = fleets[fid];
            if (!f.alive || f.ships.empty()) continue;
            double scan = 0;
            for (Id s : f.ships) scan = std::max(scan, stats(ships[s]).scan);
            int radius = scan >= 1.6 ? 2 : scan >= 0.8 ? 1 : 0;
            if (f.order.kind == OrderKind::Scout && f.system == f.order.system) radius = std::min(3, radius + 1);
            mark(f.system, radius, 2);
            if (f.act == Act::Jump && f.jumpTo != NONE) mark(f.jumpTo, 0, 2);
        }
        for (Id o : outpostIds) {
            const Station& st = stations[o];
            if (!st.dead && st.owner == corp && st.state != OutpostState::Anchoring) mark(st.system, 2, 2);
        }
    }
    mark(stations[pc().home].system, 1, 2);  // headquarters: local traffic control
    for (auto& s : systems)  // Deep Survey Arrays
        if (s.surveyArray == player) mark(s.id, 3, 2);
    for (auto& c : colonies)  // colonies watch their own system; a spaceport looks further
        if (c.owner == player && !stations[c.station].dead) mark(c.system, c.cmod[CMOD_SPACEPORT] > 0 ? 1 : 0, 2);
    for (size_t r = 0; r < regions.size() && r < regionReveal.size(); r++)
        if (regionReveal[r] > now)
            for (Id sy : regionSystems[r]) sight[sy] = std::max<uint8_t>(sight[sy], 1);
}

void World::snapshotStation(Id station) {
    const Station& st = stations[station];
    if (st.dead) return;
    MarketSnap& m = intel[station];
    m.t = now;
    m.rows.clear();
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (st.target[i] <= 0 && st.bookOf[i] < 0) continue;
        SnapRow r;
        r.item = (int16_t)i;
        r.ask = bestAsk(station, i, &r.askQty);
        r.bid = bestBid(station, i, &r.bidQty);
        if (r.ask <= 0 && r.bid <= 0) continue;
        m.rows.push_back(r);
    }
}

// The Charter Exchange's daily close: every Charter trade hub's prices.
void World::intelDaily() {
    if (!fog) return;
    for (Id h : hubs)
        if (systems[stations[h].system].zone == Zone::Charter) snapshotStation(h);
}

Money World::intelPrice(Id region, bool fleetsReport) const {
    if (region < 0 || region >= (Id)regions.size()) return 0;
    int st = 0;
    for (Id sy : regionSystems[region]) st += (int)systems[sy].stations.size();
    if (fleetsReport) return (150000 + 8000 * (Money)regionSystems[region].size()) * CR;
    return (50000 + 30000 * (Money)st) * CR;
}

Result World::buyIntel(Id corp, Id region, bool fleetsReport) {
    if (!corps[corp].player) return Result::fail("Only the player buys intel.");
    if (!fog) return Result::fail("Fog of war is off in this game: you already see everything.");
    if (region < 0 || region >= (Id)regions.size()) return Result::fail("Pick a region.");
    Money price = intelPrice(region, fleetsReport);
    if (corps[corp].wallet < price) return Result::fail("The broker wants " + fmtMoney(price) + ".");
    pay(corp, -price, Cat::Other, std::string(fleetsReport ? "Fleet sightings: " : "Market report: ") + regions[region].name);
    if (fleetsReport) {
        if (regionReveal.size() != regions.size()) regionReveal.assign(regions.size(), 0);
        regionReveal[region] = std::max(regionReveal[region], now) + MINUTES_PER_DAY;
        updateSight();
        return Result::good("Brokers will relay every fleet sighted in " + regions[region].name + " for the next 24 hours.");
    }
    int n = 0;
    for (Id sy : regionSystems[region])
        for (Id st : systems[sy].stations)
            if (!stations[st].dead) { snapshotStation(st); n++; }
    return Result::good("Market report for " + regions[region].name + ": prices at " + std::to_string(n) + " stations, as of now.");
}

}  // namespace nb
