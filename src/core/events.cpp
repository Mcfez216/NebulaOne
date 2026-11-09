// NebulaOne - the living galaxy: shortages, gluts, strikes, booms, outbreaks,
// Charter edicts, ion storms, raider surges, gold rushes and Warden crackdowns.
//
// Events change the inputs of the station economies (consumption, production,
// stock), the law (levy, embargo, Warden response) and travel (storms close a
// system's lanes). Several are announced before they start, so the wire is
// worth reading: buy before the strike, sell into the outbreak.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "world.h"

namespace nb {

const char* eventKindName(EventKind k) {
    switch (k) {
        case EventKind::Shortage: return "Shortage";
        case EventKind::Glut: return "Glut";
        case EventKind::Strike: return "Strike";
        case EventKind::Boom: return "Boom";
        case EventKind::Plague: return "Outbreak";
        case EventKind::Levy: return "Levy edict";
        case EventKind::Embargo: return "Embargo";
        case EventKind::Storm: return "Ion storm";
        case EventKind::PirateSurge: return "Raider surge";
        case EventKind::GoldRush: return "Gold rush";
        case EventKind::Crackdown: return "Crackdown";
        default: break;
    }
    return "?";
}

bool World::embargoed(Id station, int it) const {
    if (it < 0 || it >= ITEM_COUNT || !embargo[it]) return false;
    return systems[stations[station].system].zone == Zone::Charter;
}

const GalaxyEvent* World::eventAt(Id station, int it) const {
    const Station& st = stations[station];
    Id region = systems[st.system].region;
    for (auto& e : events) {
        if (!e.applied || e.over) continue;
        switch (e.kind) {
            case EventKind::Shortage:
            case EventKind::Glut:
                if (e.station == station && e.item == it) return &e;
                break;
            case EventKind::Strike:
                if (e.station == station && st.prod[it] > 0) return &e;
                break;
            case EventKind::Boom:
                if (e.region == region && (it == LUXURY_GOODS || it == ELECTRONICS || it == FUEL_CELLS)) return &e;
                break;
            case EventKind::Plague:
                if (e.region == region && (it == MEDICAL_KITS || it == RATIONS)) return &e;
                break;
            case EventKind::Embargo:
                if (e.item == it) return &e;
                break;
            default: break;
        }
    }
    return nullptr;
}

void World::rebuildEventIndex() {
    stormUntil.assign(systems.size(), 0);
    crackdown.assign(regions.size(), 0);
    embargo.fill(false);
    for (auto& e : events) {
        if (!e.applied || e.over) continue;
        if (e.kind == EventKind::Storm && e.system != NONE) stormUntil[e.system] = std::max(stormUntil[e.system], e.end);
        if (e.kind == EventKind::Crackdown && e.region != NONE) crackdown[e.region] = 1;
        if (e.kind == EventKind::Embargo && e.item >= 0) embargo[e.item] = true;
    }
}

static void scaleRegion(World& w, Id region, int it, float mul) {
    for (Id sy : w.regionSystems[region])
        for (Id st : w.systems[sy].stations) w.stations[st].eventMul[it] *= mul;
}

void World::applyEvent(GalaxyEvent& e, bool on) {
    switch (e.kind) {
        case EventKind::Shortage:
            stations[e.station].eventMul[e.item] *= on ? e.mag : 1.0f / e.mag;
            break;
        case EventKind::Glut: {
            Station& st = stations[e.station];
            st.eventMul[e.item] *= on ? 0.25f : 4.0f;
            if (on) st.stock[e.item] = std::min(st.target[e.item] * 4.0f, st.stock[e.item] + st.target[e.item] * e.mag);
            break;
        }
        case EventKind::Strike:
            stations[e.station].prodMul = on ? 0.0f : 1.0f;
            break;
        case EventKind::Boom: {
            float m = on ? e.mag : 1.0f / e.mag, h = on ? std::sqrt(e.mag) : 1.0f / std::sqrt(e.mag);
            scaleRegion(*this, e.region, LUXURY_GOODS, m);
            scaleRegion(*this, e.region, ELECTRONICS, m);
            scaleRegion(*this, e.region, FUEL_CELLS, h);
            break;
        }
        case EventKind::Plague:
            scaleRegion(*this, e.region, MEDICAL_KITS, on ? e.mag : 1.0f / e.mag);
            scaleRegion(*this, e.region, RATIONS, on ? 1.4f : 1.0f / 1.4f);
            break;
        case EventKind::Levy:
            charterLevy = on ? e.mag : 0.01f;
            break;
        case EventKind::Embargo:
            // smugglers bid the banned goods up everywhere the Charter does not reach
            for (auto& st : stations)
                if (!st.dead && systems[st.system].zone != Zone::Charter && st.target[e.item] > 0)
                    st.eventMul[e.item] *= on ? 1.6f : 1.0f / 1.6f;
            break;
        case EventKind::GoldRush: {
            StarSystem& s = systems[e.system];
            if (on) {
                Belt b;
                b.ore = e.item;
                b.capacity = b.remaining = e.mag;
                b.until = e.end;
                // reuse a played-out slot so belt indices stay stable and the list stays short
                int slot = -1;
                for (int k = 0; k < (int)s.belts.size(); k++)
                    if (s.belts[k].until != 0 && s.belts[k].until < now && s.belts[k].capacity <= 0) { slot = k; break; }
                if (slot < 0) { slot = (int)s.belts.size(); s.belts.push_back(b); }
                else s.belts[slot] = b;
                e.belt = slot;
            } else if (e.belt >= 0 && e.belt < (int)s.belts.size()) {
                s.belts[e.belt].capacity = 0;
                s.belts[e.belt].remaining = 0;
            }
            break;
        }
        default: break;  // storms, surges and crackdowns work through the index
    }
}

Id World::postEvent(EventKind k, Id where, int it, Tick lead, int days, float mag) {
    GalaxyEvent e;
    e.id = events.empty() ? 0 : events.back().id + 1;
    e.kind = k;
    e.item = it;
    e.mag = mag;
    e.posted = now;
    e.start = now + lead;
    e.end = e.start + (k == EventKind::Storm ? (Tick)days * 60 : (Tick)days * MINUTES_PER_DAY);  // storms: hours
    switch (k) {
        case EventKind::Shortage: case EventKind::Glut: case EventKind::Strike:
            e.station = where;
            e.system = stations[where].system;
            e.region = systems[e.system].region;
            break;
        case EventKind::Boom: case EventKind::Plague: case EventKind::PirateSurge: case EventKind::Crackdown:
            e.region = where;
            break;
        case EventKind::Storm: case EventKind::GoldRush:
            e.system = where;
            e.region = systems[where].region;
            break;
        default: break;
    }
    char b[320];
    std::string sn = e.station != NONE ? stations[e.station].name : "";
    std::string sy = e.system != NONE ? systems[e.system].name : "";
    std::string rg = e.region != NONE ? regions[e.region].name : "";
    std::string when = fmtDate(e.start).substr(0, 10);
    switch (k) {
        case EventKind::Shortage: {
            static const char* goodText[GOOD_COUNT] = {"Crop blight leaves %s short of food: Ration Packs demand soars",
                                                       "Reactor fault at %s: Fuel Cells needed urgently",
                                                       "Injuries pile up at %s: Medical Kits demand spikes",
                                                       "Sensor refit programme at %s drives demand for Electronics",
                                                       "Festival season at %s: Luxury Goods flying off the shelves"};
            if (isGood(it)) std::snprintf(b, sizeof b, goodText[it - GOOD_FIRST], sn.c_str());
            else std::snprintf(b, sizeof b, "Order book at %s swallows %s: demand up %.0fx", sn.c_str(), item(it).name.c_str(), mag);
            break;
        }
        case EventKind::Glut:
            std::snprintf(b, sizeof b, "A convoy floods %s with %s: prices sag and demand dries up", sn.c_str(), item(it).name.c_str());
            break;
        case EventKind::Strike:
            std::snprintf(b, sizeof b, "Dockworkers at %s vote to strike from %s: production stops for %d days", sn.c_str(), when.c_str(), days);
            break;
        case EventKind::Boom:
            std::snprintf(b, sizeof b, "Boom in %s: Luxury Goods and Electronics demand surges across the region", rg.c_str());
            break;
        case EventKind::Plague: {
            static const char* fevers[] = {"Vessel fever", "Grey lung", "Drift sickness", "Halo plague", "Copper cough"};
            std::snprintf(b, sizeof b, "Outbreak of %s in %s: Medical Kits wanted at every station", fevers[rng.irange(0, 4)], rg.c_str());
            break;
        }
        case EventKind::Levy:
            if (mag > 0.01f) std::snprintf(b, sizeof b, "Charter Authority raises its trade levy to %.0f%% from %s for %d days", mag * 100, when.c_str(), days);
            else std::snprintf(b, sizeof b, "Charter Authority suspends its trade levy from %s for %d days", when.c_str(), days);
            break;
        case EventKind::Embargo:
            std::snprintf(b, sizeof b, "Charter Authority embargoes %s in Charter space from %s for %d days; smugglers' prices rise",
                          item(it).name.c_str(), when.c_str(), days);
            break;
        case EventKind::Storm:
            std::snprintf(b, sizeof b, "Ion storm forecast over %s: its lanes close from %s for %d hours", sy.c_str(), fmtDate(e.start).c_str(), days);
            break;
        case EventKind::PirateSurge:
            std::snprintf(b, sizeof b, "Raider bands gather in %s: underwriters warn of attacks for the next %d days", rg.c_str(), days);
            break;
        case EventKind::GoldRush:
            std::snprintf(b, sizeof b, "Prospectors report a rich %s pocket in %s (%s): the rush is on", item(it).name.c_str(), sy.c_str(),
                          zoneLong(systems[e.system].zone));
            break;
        case EventKind::Crackdown:
            std::snprintf(b, sizeof b, "Charter Wardens crack down in %s for %d days: they answer in the first round and bounties double",
                          rg.c_str(), days);
            break;
        default: b[0] = 0;
    }
    e.headline = b;
    events.push_back(e);
    GalaxyEvent& ev = events.back();
    if (lead <= 0) {
        applyEvent(ev, true);
        ev.applied = true;
        rebuildEventIndex();
    }
    // the wire carries galaxy-wide news, everything near the player, and some of the rest
    bool global = k == EventKind::Levy || k == EventKind::Embargo;
    Id homeSys = stations[pc().home].system;
    Id at = ev.system != NONE ? ev.system : ev.region != NONE && !regionSystems[ev.region].empty() ? regionSystems[ev.region][0] : NONE;
    bool near = at != NONE && jumps(homeSys, at) <= 18;
    if (global || near || rng.chance(0.2)) {
        Sev sv = k == EventKind::Storm || k == EventKind::PirateSurge || k == EventKind::Strike ? Sev::Warn : Sev::Info;
        addLog(Chan::News, sv, false, at, ev.headline + ".");
    }
    return ev.id;
}

void World::eventsHourly() {
    bool changed = false;
    for (auto& e : events) {
        if (e.over) continue;
        if (!e.applied && now >= e.start) {
            applyEvent(e, true);
            e.applied = true;
            changed = true;
            if (e.kind == EventKind::Strike) addLog(Chan::News, Sev::Warn, false, e.system, "The strike at " + stations[e.station].name + " has begun.");
            if (e.kind == EventKind::Storm) addLog(Chan::News, Sev::Warn, false, e.system, "Ion storm over " + systems[e.system].name + ": its lanes are closed.");
            if (e.kind == EventKind::Embargo) addLog(Chan::News, Sev::Info, false, NONE, "The Charter embargo on " + item(e.item).name + " is now in force.");
        }
        if (e.applied && now >= e.end) {
            applyEvent(e, false);
            e.over = true;
            changed = true;
            if (e.kind == EventKind::Strike) addLog(Chan::News, Sev::Info, false, e.system, "Strike over at " + stations[e.station].name + ": production resumes.");
            if (e.kind == EventKind::Storm) addLog(Chan::News, Sev::Info, false, e.system, "The storm over " + systems[e.system].name + " has passed.");
            if (e.kind == EventKind::Embargo) addLog(Chan::News, Sev::Info, false, NONE, "The Charter lifts its embargo on " + item(e.item).name + ".");
            if (e.kind == EventKind::Levy) addLog(Chan::News, Sev::Info, false, NONE, "The Charter trade levy returns to 1%.");
            if (e.kind == EventKind::GoldRush) addLog(Chan::News, Sev::Info, false, e.system, "The " + item(e.item).name + " pocket in " + systems[e.system].name + " is played out.");
        }
    }
    if (changed) rebuildEventIndex();
}

void World::eventsDaily() {
    // forget events that ended more than ten days ago
    events.erase(std::remove_if(events.begin(), events.end(), [&](const GalaxyEvent& e) { return e.over && now - e.end > 10 * MINUTES_PER_DAY; }),
                 events.end());
    // raider surges keep the heat up while they last
    for (auto& e : events)
        if (e.applied && !e.over && e.kind == EventKind::PirateSurge)
            for (Id sy : regionSystems[e.region])
                if (systems[sy].zone != Zone::Charter) systems[sy].heat += 0.6f;

    int active = 0;
    bool edict = false;
    for (auto& e : events)
        if (!e.over) {
            active++;
            edict = edict || e.kind == EventKind::Levy || e.kind == EventKind::Embargo;
        }
    int cap = 4 + (int)stations.size() / 60;
    double rate = 0.8 + stations.size() / 250.0;
    int n = (int)rate + (rng.chance(rate - (int)rate) ? 1 : 0);
    Id homeSys = stations[pc().home].system;
    std::vector<Id> near = systemsWithin(homeSys, 12);
    auto pickSystem = [&]() -> Id {
        if (rng.chance(0.6) && !near.empty()) return near[rng.irange(0, (int)near.size() - 1)];
        return (Id)rng.irange(0, (int)systems.size() - 1);
    };
    auto pickStation = [&](auto ok) -> Id {
        for (int t = 0; t < 12; t++) {
            const StarSystem& s = systems[pickSystem()];
            if (s.stations.empty()) continue;
            Id st = s.stations[rng.irange(0, (int)s.stations.size() - 1)];
            if (!stations[st].dead && !stations[st].isOutpost() && ok(stations[st])) return st;
        }
        return NONE;
    };
    static const int weight[EVENT_KIND_COUNT] = {30, 10, 9, 6, 5, 2, 2, 12, 8, 7, 5};
    int total = 0;
    for (int w : weight) total += w;
    for (int k = 0; k < n && active < cap; k++) {
        int roll = rng.irange(0, total - 1), kind = 0;
        while (roll >= weight[kind]) roll -= weight[kind++];
        EventKind ek = (EventKind)kind;
        switch (ek) {
            case EventKind::Shortage: {
                Id st = pickStation([](const Station&) { return true; });
                if (st == NONE) break;
                std::vector<int> cands;
                for (int i = GOOD_FIRST; i < MODULE_FIRST; i++)
                    if (stations[st].cons[i] > 0) cands.push_back(i);
                for (int i = MINERAL_FIRST; i < GOOD_FIRST; i++)
                    if (stations[st].cons[i] > 0 && rng.chance(0.3)) cands.push_back(i);
                if (cands.empty()) break;
                int it = cands[rng.irange(0, (int)cands.size() - 1)];
                postEvent(ek, st, it, 0, rng.irange(3, 7), (float)rng.range(2.5, 4.0));
                active++;
                break;
            }
            case EventKind::Glut: {
                Id st = pickStation([](const Station&) { return true; });
                if (st == NONE) break;
                std::vector<int> cands;
                for (int i = GOOD_FIRST; i < COMPONENT_FIRST; i++)
                    if (stations[st].target[i] > 0) cands.push_back(i);
                if (cands.empty()) break;
                postEvent(ek, st, cands[rng.irange(0, (int)cands.size() - 1)], 0, rng.irange(3, 6), (float)rng.range(1.0, 2.0));
                active++;
                break;
            }
            case EventKind::Strike: {
                Id st = pickStation([](const Station& s) {
                    return s.type == StationType::Industrial || s.type == StationType::Agricultural || s.type == StationType::MiningColony;
                });
                if (st == NONE) break;
                bool already = false;
                for (auto& e : events)
                    if (!e.over && e.kind == EventKind::Strike && e.station == st) already = true;
                if (already) break;
                postEvent(ek, st, -1, rng.irange(1, 2) * MINUTES_PER_DAY, rng.irange(3, 6), 0);
                active++;
                break;
            }
            case EventKind::Boom:
            case EventKind::Plague:
            case EventKind::PirateSurge: {
                Id r = systems[pickSystem()].region;
                if (r == NONE) break;
                bool dup = false;
                for (auto& e : events)
                    if (!e.over && e.kind == ek && e.region == r) dup = true;
                if (dup) break;
                float mag = ek == EventKind::Boom ? (float)rng.range(1.6, 2.2) : ek == EventKind::Plague ? (float)rng.range(2.5, 3.5) : 1.0f;
                postEvent(ek, r, -1, 0, rng.irange(4, 9), mag);
                active++;
                break;
            }
            case EventKind::Crackdown: {
                Id sy = pickSystem();
                if (systems[sy].zone != Zone::Charter) break;
                Id r = systems[sy].region;
                if (r == NONE || crackdown[r]) break;
                postEvent(ek, r, -1, 0, rng.irange(5, 10), 1.0f);
                active++;
                break;
            }
            case EventKind::Levy:
            case EventKind::Embargo: {
                if (edict) break;
                if (ek == EventKind::Levy) postEvent(ek, NONE, -1, 2 * MINUTES_PER_DAY, rng.irange(7, 14), rng.chance(0.6) ? 0.03f : 0.0f);
                else {
                    static const int banned[] = {LUXURY_GOODS, ELECTRONICS, NOCTURNITE, ZYRIUM};
                    postEvent(ek, NONE, banned[rng.irange(0, 3)], 2 * MINUTES_PER_DAY, rng.irange(7, 12), 1.0f);
                }
                edict = true;
                active++;
                break;
            }
            case EventKind::Storm: {
                Id sy = pickSystem();
                if (stormBlocked(sy)) break;
                postEvent(ek, sy, -1, rng.irange(6, 12) * MINUTES_PER_HOUR, rng.irange(12, 40), 1.0f);
                active++;
                break;
            }
            case EventKind::GoldRush: {
                Id sy = NONE;
                for (int t = 0; t < 10 && sy == NONE; t++) {
                    Id c = pickSystem();
                    if (systems[c].zone == Zone::Frontier || (systems[c].zone == Zone::Contract && systems[c].depth >= 2)) sy = c;
                }
                if (sy == NONE) break;
                bool deep = systems[sy].zone == Zone::Frontier;
                int ore = deep ? (rng.chance(0.4) ? AETHERITE : NOCTYX) : (rng.chance(0.5) ? ZYRKON : LUMINAR);
                postEvent(ek, sy, ore, 0, rng.irange(6, 12), (float)(rng.range(250000, 600000) * (deep ? 1.0 : 1.6)));
                active++;
                break;
            }
            default: break;
        }
    }
}

}  // namespace nb
