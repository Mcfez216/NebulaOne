// NebulaOne - exploration: surveys, hidden sites and salvage.
//
// Every system hides a few sites each fortnight, worked out from the seed so
// nothing is stored until someone finds one. A Survey order scans a system;
// the stronger the fleet's survey array (Wisp hulls, Survey Scanners), the
// faster it works and the more it finds. What it finds:
//   Derelict    - a wreck to salvage for components, modules and copies
//   Relic cache - rare minerals and old research data (a free ME/TE level)
//   Ore pocket  - a rich hidden belt that only the finder knows about
//   Drift lane  - a short-lived shortcut to a distant system, finder only
// Sites are first come, first served: rival corporations survey too.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

const char* siteKindName(SiteKind k) {
    switch (k) {
        case SiteKind::Derelict: return "Derelict";
        case SiteKind::Relic: return "Relic cache";
        case SiteKind::Pocket: return "Ore pocket";
        case SiteKind::Drift: return "Drift lane";
    }
    return "?";
}

std::vector<Site> World::hiddenSites(Id system, int epoch) const {
    std::vector<Site> out;
    if (system < 0 || system >= (Id)systems.size()) return out;
    Rng r;
    r.seed(seed * 0x9E3779B97F4A7C15ull ^ ((uint64_t)system * 0xC2B2AE3D27D4EB4Full) ^ ((uint64_t)(epoch + 1) * 0x165667B19E3779F9ull));
    const StarSystem& s = systems[system];
    int n = 0;
    switch (s.zone) {
        case Zone::Charter: n = r.chance(0.25) ? 1 : 0; break;
        case Zone::Contract: n = (r.chance(0.5) ? 1 : 0) + (r.chance(0.15) ? 1 : 0); break;
        case Zone::Frontier: n = 1 + (r.chance(0.5) ? 1 : 0) + (r.chance(0.15) ? 1 : 0); break;
    }
    for (int k = 0; k < n; k++) {
        Site st;
        st.system = system;
        st.key = ((uint64_t)(uint32_t)system << 28) | ((uint64_t)(epoch & 0xFFFFF) << 4) | (uint64_t)k;
        st.level = s.zone == Zone::Charter ? 0 : std::clamp(s.depth + (r.chance(0.3) ? 1 : 0), 1, 3);
        int roll = r.irange(0, 99);
        if (s.zone == Zone::Charter) st.kind = roll < 80 ? SiteKind::Derelict : SiteKind::Pocket;
        else if (s.zone == Zone::Contract)
            st.kind = roll < 45 ? SiteKind::Derelict : roll < 60 ? SiteKind::Relic : roll < 85 ? SiteKind::Pocket : SiteKind::Drift;
        else st.kind = roll < 30 ? SiteKind::Derelict : roll < 60 ? SiteKind::Relic : roll < 80 ? SiteKind::Pocket : SiteKind::Drift;
        switch (st.kind) {
            case SiteKind::Derelict: {
                static const int byLevel[4][2] = {{MATTOCK, DROVER}, {LANCER, MATTOCK}, {QUARRIER, LANCER}, {CARRACK, WARDEN}};
                st.name = "Derelict " + shipDef(byLevel[st.level][r.irange(0, 1)]).name;
                break;
            }
            case SiteKind::Relic: {
                static const char* names[] = {"Pre-Charter relay", "Ancient survey beacon", "Abandoned research hulk", "Sealed cargo vault",
                                              "Silent listening post"};
                st.name = names[r.irange(0, 4)];
                break;
            }
            case SiteKind::Pocket: st.name = "Ore pocket"; break;
            case SiteKind::Drift: st.name = "Drift lane"; break;
        }
        out.push_back(st);
    }
    return out;
}

Id World::discoverSite(Id corp, const Site& proto) {
    Site st = proto;
    st.id = sites.empty() ? 0 : sites.back().id + 1;
    st.finder = corp;
    st.found = now;
    st.expires = now + 12 * MINUTES_PER_DAY;
    StarSystem& sys = systems[st.system];
    if (st.kind == SiteKind::Pocket) {
        int want = std::min(3, st.level + 1);
        std::vector<int> ores;
        for (int o = 0; o < ORE_COUNT; o++)
            if (item(o).depth == want) ores.push_back(o);
        Belt b;
        b.ore = ores[rng.irange(0, (int)ores.size() - 1)];
        b.capacity = b.remaining = rng.range(120000, 300000) * (1 + st.level);
        b.knownBy = corp;
        st.expires = now + 20 * MINUTES_PER_DAY;
        b.until = st.expires;
        int slot = -1;
        for (int k = 0; k < (int)sys.belts.size(); k++)
            if (sys.belts[k].until != 0 && sys.belts[k].until < now && sys.belts[k].capacity <= 0) { slot = k; break; }
        if (slot < 0) { slot = (int)sys.belts.size(); sys.belts.push_back(b); }
        else sys.belts[slot] = b;
        st.belt = slot;
        st.name = "Hidden " + item(b.ore).name + " pocket";
    }
    if (st.kind == SiteKind::Drift) {
        Id far = NONE;
        for (int t = 0; t < 60 && far == NONE; t++) {
            Id c = (Id)rng.irange(0, (int)systems.size() - 1);
            int d = hexDistance(sys.hex, systems[c].hex);
            if (d >= 10 && d <= 40 && c != st.system && jumps(st.system, c) >= 5) far = c;
        }
        if (far == NONE) {
            st.kind = SiteKind::Derelict;
            st.name = "Derelict " + shipDef(LANCER).name;
        } else {
            st.link = far;
            st.hexes = 3 + hexDistance(sys.hex, systems[far].hex) / 8;
            st.expires = now + rng.irange(3, 6) * MINUTES_PER_DAY;
            st.name = "Drift lane to " + systems[far].name;
        }
    }
    sites.push_back(st);
    siteKeys[st.key] = 1;
    if (st.kind == SiteKind::Drift) rebuildDrift();
    return st.id;
}

void World::rebuildDrift() {
    drift.clear();
    for (auto& s : sites)
        if (s.kind == SiteKind::Drift && !s.done && s.expires > now && s.link != NONE) drift.push_back({s.system, s.link, s.finder, s.hexes});
}

static double fleetScan(const World& w, const Fleet& f) {
    double best = 0, rest = 0;
    for (Id s : f.ships) {
        double v = w.stats(w.ships[s]).scan;
        if (v > best) { rest += best; best = v; }
        else rest += v;
    }
    return best + 0.15 * rest;  // a second survey ship helps a little
}

void World::surveyComplete(Fleet& f) {
    Id sys = f.system;
    int epoch = fortnight();
    double scan = fleetScan(*this, f);
    std::vector<std::string> found;
    for (const Site& c : hiddenSites(sys, epoch)) {
        if (siteKeys.count(c.key)) continue;
        double p = std::clamp(0.20 + 0.30 * scan - 0.10 * c.level, 0.08, 0.90);
        if (!rng.chance(p)) continue;
        Id id = discoverSite(f.corp, c);
        for (auto& s : sites)
            if (s.id == id) found.push_back(s.name);
    }
    if (corps[f.corp].player) {
        surveyed[sys] = epoch;
        pstats.surveys++;
        pstats.sitesFound += (int64_t)found.size();
        std::string msg = "Survey of " + systems[sys].name + " complete: ";
        if (found.empty()) msg += "nothing new this fortnight.";
        else {
            for (size_t k = 0; k < found.size(); k++) msg += (k ? ", " : "") + found[k];
            msg += ".";
        }
        addLog(Chan::Corp, found.empty() ? Sev::Info : Sev::Good, true, sys, msg);
    }
    FleetOrder& o = f.order;
    if (o.loop) {
        // keep going: the nearest system within three jumps not surveyed this fortnight
        std::vector<int> d;
        std::vector<Id> ring = systemsWithin(sys, 3, &d);
        Id next = NONE;
        for (size_t k = 1; k < ring.size(); k++) {
            auto it = surveyed.find(ring[k]);
            bool done = corps[f.corp].player && it != surveyed.end() && it->second == epoch;
            if (!done && !stormBlocked(ring[k])) { next = ring[k]; break; }
        }
        if (next != NONE) {
            o.system = next;
            f.status = "Survey: moving on to " + systems[next].name;
            return;
        }
    }
    f.phase = 99;
    f.status = found.empty() ? "Survey complete: nothing found" : "Survey complete: " + std::to_string(found.size()) + " found";
}

static void lootTable(World& w, const Site& s, std::vector<std::pair<int, int64_t>>& out, double* valueOut) {
    static const double base[4] = {100000, 400000, 1200000, 3000000};
    double target = base[std::clamp(s.level, 0, 3)] * w.rng.range(0.6, 1.4);
    double value = 0;
    auto add = [&](int it, double budget) {
        double unit = toCr(item(it).base);
        if (unit <= 0) return;
        int64_t q = std::max<int64_t>(1, (int64_t)(budget / unit));
        out.push_back({it, q});
        value += q * unit;
    };
    if (s.kind == SiteKind::Derelict) {
        for (int k = 0; k < 4 && value < target * 0.9; k++) add(COMPONENT_FIRST + w.rng.irange(0, COMPONENT_COUNT - 1), target * 0.22);
        if (w.rng.chance(0.55)) add(MODULE_FIRST + w.rng.irange(0, MODULE_COUNT - 1), 1);
        if (w.rng.chance(0.25)) add(BPC_FIRST + w.rng.irange(0, SHIP_COUNT - 1), 1);
        add(FUEL_CELLS, target * 0.08);
    } else {
        int hi = s.level >= 2 ? AETHERIUM : ZYRIUM, lo = s.level >= 2 ? NOCTURNITE : LUCENITE;
        add(hi, target * 0.6);
        add(lo, target * 0.5);
        if (w.rng.chance(0.35)) add(ELECTRONICS, target * 0.15);
    }
    if (valueOut) *valueOut = value;
}

void World::salvageComplete(Fleet& f) {
    FleetOrder& o = f.order;
    Site* s = nullptr;
    for (auto& x : sites)
        if (x.id == o.site) s = &x;
    if (!s || s->done || s->expires <= now || s->system != f.system) {
        f.phase = 99;
        f.status = "The site is gone";
        return;
    }
    std::vector<std::pair<int, int64_t>> loot;
    double value = 0;
    lootTable(*this, *s, loot, &value);
    int64_t left = 0;
    for (auto& [it, q] : loot) {
        int64_t room = (int64_t)(fleetFreeFor(f, it) / std::max(1e-9, item(it).volume));
        int64_t n = std::min(q, room);
        if (n > 0) f.cargo[it] += n;
        left += q - n;
    }
    std::string extra;
    if (s->kind == SiteKind::Relic && rng.chance(0.45)) {
        Corp& c = corps[f.corp];
        int b = rng.irange(0, BLUEPRINT_COUNT - 1);
        bool me = rng.chance(0.5);
        uint8_t& lvl = me ? c.me[b] : c.te[b];
        if (lvl < 10) {
            lvl++;
            extra = "; old research data raised your " + item(blueprint(b).product).name + " blueprint's " + (me ? "ME" : "TE") + " to " +
                    std::to_string(lvl);
        }
    }
    s->done = true;
    if (corps[f.corp].player) {
        pstats.salvaged++;
        addLog(Chan::Corp, Sev::Good, true, f.system,
               f.name + " salvaged the " + s->name + " in " + systems[f.system].name + ": about " + fmtMoneyShort(credits(value)) + " aboard" +
                   (left > 0 ? " (the hold could not take it all)" : "") + extra + ".");
        if (value >= 2e6) addLog(Chan::News, Sev::Info, false, f.system, corps[f.corp].name + " recovered a " + s->name + " in " + systems[f.system].name + ".");
    }
    f.phase = 1;  // take it home
    f.status = "Salvage aboard";
}

void World::aiExplore(Corp& c) {
    std::vector<Id> ring = systemsWithin(stations[c.home].system, 6);
    if (ring.empty()) return;
    Id sys = ring[rng.irange(0, (int)ring.size() - 1)];
    if (systems[sys].zone == Zone::Charter && rng.chance(0.6)) return;  // the pickings are thin in Charter space
    for (const Site& cand : hiddenSites(sys, fortnight())) {
        if (siteKeys.count(cand.key) || cand.kind == SiteKind::Drift) continue;
        if (!rng.chance(0.45)) continue;
        Id id = discoverSite(c.id, cand);
        Site* s = nullptr;
        for (auto& x : sites)
            if (x.id == id) s = &x;
        if (!s || s->kind == SiteKind::Pocket) continue;  // AI miners will work the pocket
        std::vector<std::pair<int, int64_t>> loot;
        double value = 0;
        lootTable(*this, *s, loot, &value);
        pay(c.id, credits(value * 0.7), Cat::Other, "Salvage: " + s->name);
        s->done = true;
        Id homeSys = stations[pc().home].system;
        if (value >= 1.5e6 && (jumps(homeSys, sys) <= 15 || rng.chance(0.2)))
            addLog(Chan::News, Sev::Info, false, sys, c.name + " recovered a " + s->name + " in " + systems[sys].name + ".");
    }
}

void World::exploreDaily() {
    bool driftChanged = false;
    for (auto& s : sites) {
        if (s.done || s.expires > now) continue;
        s.done = true;
        if (s.kind == SiteKind::Pocket && s.belt >= 0 && s.belt < (int)systems[s.system].belts.size()) {
            Belt& b = systems[s.system].belts[s.belt];
            b.capacity = 0;
            b.remaining = 0;
        }
        if (s.kind == SiteKind::Drift) {
            driftChanged = true;
            if (corps[s.finder].player) addLog(Chan::Corp, Sev::Info, true, s.system, "The " + s.name + " has closed.");
        }
    }
    if (driftChanged) rebuildDrift();
    // sites from before last fortnight are history
    int epoch = fortnight();
    sites.erase(std::remove_if(sites.begin(), sites.end(),
                               [&](const Site& s) {
                                   int e = (int)((s.key >> 4) & 0xFFFFF);
                                   return s.done && e < epoch - 1 && now - s.found > 30 * MINUTES_PER_DAY;
                               }),
                sites.end());
    siteKeys.clear();
    for (auto& s : sites) siteKeys[s.key] = 1;
    for (auto& c : corps)
        if (c.alive && !c.player && (c.arch == Arch::Miner || c.arch == Arch::Trader || c.arch == Arch::Industrialist) && rng.chance(0.06))
            aiExplore(c);
}

}  // namespace nb
