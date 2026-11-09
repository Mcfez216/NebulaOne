// NebulaOne - construction: outpost module upgrades and megaprojects.
//
// A project lists the materials and credits it needs. Materials are delivered
// physically to its yard (a station near the site); credits can come from
// anyone. Whoever pays in holds a share in the result: gate tolls are split by
// share. Once fully funded, the project builds for a number of days and then
// changes the galaxy: a new lane, the Wardens' reach, a hub, a sensor array.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

const char* smodName(int m) {
    static const char* n[] = {"Refinery", "Factory", "Market Exchange", "Laboratory", "Defence Grid", "Fuel Silo"};
    return (m >= 0 && m < SMOD_COUNT) ? n[m] : "-";
}
const char* smodEffect(int m) {
    static const char* n[] = {"+4 pts refining yield and 15% faster refining per tier",
                              "10% faster manufacturing per tier",
                              "a deeper market (+50% stock and trade per tier); tier 2 draws job seekers",
                              "20% faster research and copying per tier",
                              "+60% shield and hull per tier; its guns hit besiegers",
                              "20% less fuel per tier"};
    return (m >= 0 && m < SMOD_COUNT) ? n[m] : "";
}
const char* projectKindName(ProjectKind k) {
    switch (k) {
        case ProjectKind::Upgrade: return "Station upgrade";
        case ProjectKind::JumpGate: return "Jump Gate";
        case ProjectKind::Beacon: return "Charter Beacon";
        case ProjectKind::Nexus: return "Trade Nexus";
        case ProjectKind::SurveyArray: return "Deep Survey Array";
        case ProjectKind::ColonyUpgrade: return "Colony upgrade";
        default: return "-";
    }
}

double Project::progress() const {
    double want = (double)cash, have_ = (double)cashGot;
    for (size_t i = 0; i < need.size(); i++) {
        double v = (double)item(need[i].first).base;
        want += v * need[i].second;
        have_ += v * std::min(got[i], need[i].second);
    }
    return want > 0 ? std::min(1.0, have_ / want) : 1.0;
}

double World::smodBonus(Id station, int module) const {
    if (station == NONE || station >= (Id)stations.size()) return 0;
    const Station& s = stations[station];
    if (!s.isOutpost() || s.state != OutpostState::Online) return 0;
    return s.smod[module];
}

// ------------------------------------------------------------ the bill ---
std::vector<std::pair<int, int64_t>> World::projectNeeds(ProjectKind kind, int module, int tier) const {
    switch (kind) {
        case ProjectKind::Upgrade: {
            int64_t k = tier <= 1 ? 1 : tier == 2 ? 2 : 4;
            switch (module) {
                case SMOD_REFINERY: return {{EXTRACTION_HEAD, 300 * k}, {POWER_CORE, 150 * k}, {HULL_PLATING, 300 * k}};
                case SMOD_FACTORY: return {{HULL_PLATING, 500 * k}, {POWER_CORE, 200 * k}, {THRUSTERS, 150 * k}};
                case SMOD_MARKET: return {{HULL_PLATING, 400 * k}, {SENSOR_ARRAY, 150 * k}, {ELECTRONICS, 400 * k}};
                case SMOD_LAB: return {{SENSOR_ARRAY, 300 * k}, {POWER_CORE, 200 * k}, {ELECTRONICS, 300 * k}};
                case SMOD_DEFENCE: return {{WEAPON_MOUNT, 400 * k}, {HULL_PLATING, 600 * k}, {POWER_CORE, 200 * k}};
                case SMOD_SILO: return {{HULL_PLATING, 300 * k}, {THRUSTERS, 100 * k}, {FUEL_CELLS, 2000 * k}};
            }
            return {};
        }
        case ProjectKind::JumpGate:
            return {{HULL_PLATING, 12000}, {THRUSTERS, 4000}, {POWER_CORE, 6000}, {SENSOR_ARRAY, 2500}, {NOCTURNITE, 6000}, {AETHERIUM, 3000}};
        case ProjectKind::Beacon: return {{WEAPON_MOUNT, 3000}, {SENSOR_ARRAY, 2000}, {HULL_PLATING, 4000}, {POWER_CORE, 2000}};
        case ProjectKind::Nexus: return {{HULL_PLATING, 5000}, {SENSOR_ARRAY, 1500}, {ELECTRONICS, 4000}, {LUXURY_GOODS, 1500}};
        case ProjectKind::SurveyArray: return {{SENSOR_ARRAY, 4000}, {POWER_CORE, 2000}, {HULL_PLATING, 3000}};
        case ProjectKind::ColonyUpgrade: {
            int64_t k = tier <= 1 ? 1 : tier == 2 ? 2 : 4;
            switch (module) {
                case CMOD_HABITAT: return {{HULL_PLATING, 600 * k}, {POWER_CORE, 200 * k}, {RATIONS, 4000 * k}, {MEDICAL_KITS, 400 * k}};
                case CMOD_EXTRACTOR: return {{EXTRACTION_HEAD, 300 * k}, {POWER_CORE, 150 * k}, {THRUSTERS, 100 * k}};
                case CMOD_SPACEPORT: return {{HULL_PLATING, 500 * k}, {SENSOR_ARRAY, 120 * k}, {THRUSTERS, 150 * k}, {ELECTRONICS, 200 * k}};
            }
            return {};
        }
        default: return {};
    }
}

Money World::projectCash(ProjectKind kind, int, int tier) const {
    switch (kind) {
        case ProjectKind::Upgrade: return (Money)4000000 * CR * tier * tier;
        case ProjectKind::JumpGate: return 250000000 * CR;
        case ProjectKind::Beacon: return 120000000 * CR;
        case ProjectKind::Nexus: return 150000000 * CR;
        case ProjectKind::SurveyArray: return 90000000 * CR;
        case ProjectKind::ColonyUpgrade: return (Money)5000000 * CR * tier * tier;
        default: return 0;
    }
}

int World::projectDays(ProjectKind kind, int tier) const {
    switch (kind) {
        case ProjectKind::Upgrade: return 2 * tier;
        case ProjectKind::JumpGate: return 20;
        case ProjectKind::Beacon: return 12;
        case ProjectKind::Nexus: return 15;
        case ProjectKind::SurveyArray: return 10;
        case ProjectKind::ColonyUpgrade: return 3 * tier;
        default: return 1;
    }
}

// The station materials go to: the nearest usable one within 3 jumps.
Id World::projectYard(Id system) const {
    std::vector<int> d;
    for (Id sy : systemsWithin(system, 3, &d))
        for (Id st : systems[sy].stations)
            if (stationUsable(st)) return st;
    return NONE;
}

std::string World::projectWhy(Id corp, ProjectKind kind, Id system, Id target, Id station) const {
    if (system < 0 || system >= (Id)systems.size()) return "Pick a system.";
    const StarSystem& s = systems[system];
    for (auto& p : projects)
        if (p.stage <= ProjectStage::Building && p.kind == kind && p.kind != ProjectKind::Upgrade &&
            (p.system == system || (kind == ProjectKind::Nexus && p.station == station)))
            return "There is already a " + std::string(projectKindName(kind)) + " under way there.";
    switch (kind) {
        case ProjectKind::JumpGate: {
            if (target < 0 || target >= (Id)systems.size() || target == system) return "Pick the system at the far end.";
            int hx = hexDistance(s.hex, systems[target].hex);
            if (hx < 8) return "Too close: a gate must span at least 8 hexes.";
            if (hx > 40) return "Too far: a gate spans at most 40 hexes.";
            for (auto& l : s.lanes)
                if (l.to == target) return "Those systems are already linked.";
            if (projectYard(system) == NONE) return "No station within 3 jumps to build from.";
            if (projectYard(target) == NONE) return "The far end needs a station within 3 jumps too.";
            return "";
        }
        case ProjectKind::Beacon:
            if (s.zone != Zone::Contract) return "A Charter Beacon goes in Contract space.";
            if (s.beacon == 2) return "There is already a beacon here.";
            if (projectYard(system) == NONE) return "No station within 3 jumps to build from.";
            return "";
        case ProjectKind::Nexus: {
            if (station < 0 || station >= (Id)stations.size()) return "Pick one of your outposts.";
            const Station& o = stations[station];
            if (!o.isOutpost() || o.owner != corp || o.dead) return "A Trade Nexus is built on one of your own outposts.";
            if (o.nexus) return "It is a Trade Nexus already.";
            if (o.smod[SMOD_MARKET] < 2) return "Upgrade its Market Exchange to tier 2 first.";
            return "";
        }
        case ProjectKind::SurveyArray:
            if (s.surveyArray != NONE) return "There is already an array here.";
            if (projectYard(system) == NONE) return "No station within 3 jumps to build from.";
            return "";
        default: return "Unknown project.";
    }
}

// ------------------------------------------------------------ commands ---
static Project makeProject(World& w, ProjectKind kind, Id corp, Id system, Id target, Id station, int module, int tier) {
    Project p;
    p.id = (Id)w.projects.size();
    p.kind = kind;
    p.founder = corp;
    p.system = system;
    p.target = target;
    p.station = station;
    p.module = module;
    p.tier = tier;
    p.need = w.projectNeeds(kind, module, tier);
    p.got.assign(p.need.size(), 0);
    p.cash = w.projectCash(kind, module, tier);
    p.buildDays = w.projectDays(kind, tier);
    p.founded = w.now;
    p.yard = kind == ProjectKind::Upgrade || kind == ProjectKind::Nexus ? station : w.projectYard(system);
    const std::string& at = w.systems[system].name;
    switch (kind) {
        case ProjectKind::Upgrade: p.name = w.stations[station].name + ": " + smodName(module) + " tier " + std::to_string(tier); break;
        case ProjectKind::JumpGate: p.name = at + " - " + w.systems[target].name + " Gate"; break;
        case ProjectKind::Beacon: p.name = at + " Charter Beacon"; break;
        case ProjectKind::Nexus: p.name = w.stations[station].name + " Trade Nexus"; break;
        case ProjectKind::SurveyArray: p.name = at + " Deep Survey Array"; break;
        default: break;
    }
    return p;
}

Result World::foundProject(Id corp, ProjectKind kind, Id system, Id target, Id station) {
    if (kind == ProjectKind::Upgrade) return Result::fail("Upgrades are started from the station.");
    if (kind == ProjectKind::Nexus && station >= 0 && station < (Id)stations.size()) system = stations[station].system;
    std::string why = projectWhy(corp, kind, system, target, station);
    if (!why.empty()) return Result::fail(why);
    Project p = makeProject(*this, kind, corp, system, target, station, -1, 0);
    // the Charter's registration fee: 5% of the credits, counted as the founder's stake
    Money fee = p.cash / 20;
    Corp& c = corps[corp];
    if (c.wallet < fee) return Result::fail("Registering the project costs " + fmtMoney(fee) + ".");
    pay(corp, -fee, Cat::Other, "Project registration: " + p.name);
    p.cashGot = fee;
    p.share[corp] += fee;
    // the Charter Authority pays 40% of a beacon's credits
    if (kind == ProjectKind::Beacon) {
        Money ch = p.cash * 40 / 100;
        p.cashGot += ch;
    }
    projects.push_back(p);
    addLog(Chan::News, Sev::Info, corp == player, system, c.name + " has founded the " + p.name + ". Anyone may contribute; backers share in the result.");
    Result r = Result::good(p.name + " founded. Deliver the materials to " + stations[p.yard].name + " and pay in the rest.");
    r.id = p.id;
    return r;
}

Result World::upgradeStation(Id corp, Id station, int module) {
    if (station < 0 || station >= (Id)stations.size() || module < 0 || module >= SMOD_COUNT) return Result::fail("Unknown upgrade.");
    Station& o = stations[station];
    if (!o.isOutpost() || o.owner != corp || o.dead) return Result::fail("Only your own outposts can be upgraded.");
    if (o.state == OutpostState::Anchoring) return Result::fail("Wait until it is online.");
    if (o.smod[module] >= SMOD_MAX_TIER) return Result::fail(std::string(smodName(module)) + " is at its top tier.");
    for (auto& p : projects)
        if (p.kind == ProjectKind::Upgrade && p.station == station && p.stage <= ProjectStage::Building)
            return Result::fail("One upgrade at a time: " + p.name + " is under way.");
    Project p = makeProject(*this, ProjectKind::Upgrade, corp, o.system, NONE, station, module, o.smod[module] + 1);
    projects.push_back(p);
    Result r = contributeProject(corp, p.id, std::min(corps[corp].wallet, p.cash), true);
    Result out = Result::good(p.name + " started. " + (r.ok ? r.msg : "Bring the materials to the station."));
    out.id = p.id;
    return out;
}

Result World::contributeProject(Id corp, Id pid, Money credits, bool goods) {
    if (pid < 0 || pid >= (Id)projects.size()) return Result::fail("Unknown project.");
    Project& p = projects[pid];
    if (p.stage != ProjectStage::Funding) return Result::fail("That project is not taking contributions.");
    if ((p.kind == ProjectKind::Upgrade || p.kind == ProjectKind::ColonyUpgrade) && corp != p.founder)
        return Result::fail("Only the owner can pay for an upgrade.");
    Corp& c = corps[corp];
    Money value = 0;
    int64_t units = 0;
    if (goods && p.yard != NONE)
        for (size_t i = 0; i < p.need.size(); i++) {
            int64_t want = p.need[i].second - p.got[i];
            int64_t take = std::min(want, have(corp, p.yard, p.need[i].first));
            if (take <= 0) continue;
            takeItem(corp, p.yard, p.need[i].first, take);
            p.got[i] += take;
            units += take;
            value += item(p.need[i].first).base * take;
        }
    Money pay_ = std::clamp<Money>(credits, 0, std::min(c.wallet, p.cash - p.cashGot));
    if (pay_ > 0) {
        pay(corp, -pay_, Cat::Other, "Contribution: " + p.name);
        p.cashGot += pay_;
        value += pay_;
    }
    if (value <= 0) return Result::fail(goods ? "Nothing to give: bring the materials to " + (p.yard != NONE ? stations[p.yard].name : std::string("the yard")) + "." : "Nothing paid.");
    p.share[corp] += value;
    std::string msg = "Contributed " + (units ? fmtInt(units) + " units" : std::string("")) + (units && pay_ ? " and " : "") + (pay_ ? fmtMoneyShort(pay_) : "") +
                      " to " + p.name + " (" + std::to_string((int)std::round(p.progress() * 100)) + "% funded).";
    if (p.progress() >= 0.9999) {
        p.stage = ProjectStage::Building;
        p.buildEnd = now + (Tick)p.buildDays * MINUTES_PER_DAY;
        addLog(Chan::News, Sev::Good, p.share.count(player) > 0, p.system, p.name + " is fully funded; construction takes " + std::to_string(p.buildDays) + " days.");
    }
    return Result::good(msg);
}

// ------------------------------------------------------------ effects ---
static void markBeacon(World& w, Id sys) {
    w.systems[sys].beacon = 2;
    for (auto& l : w.systems[sys].lanes)
        if (w.systems[l.to].zone == Zone::Contract && w.systems[l.to].beacon < 1) w.systems[l.to].beacon = 1;
}

void World::completeProject(Project& p) {
    p.stage = ProjectStage::Done;
    bool mine = p.share.count(player) > 0;
    switch (p.kind) {
        case ProjectKind::Upgrade: {
            Station& o = stations[p.station];
            if (o.dead) break;
            o.smod[p.module] = (uint8_t)p.tier;
            if (p.module == SMOD_MARKET)
                for (int i = 0; i < ITEM_COUNT; i++) { o.target[i] *= 1.5f; o.prod[i] *= 1.5f; o.cons[i] *= 1.5f; }
            if (p.module == SMOD_DEFENCE) { o.shield = std::max(o.shield, 1.0); o.hull = std::max(o.hull, 1.0); }
            addLog(Chan::Corp, Sev::Good, o.owner == player, o.system, o.name + ": " + smodName(p.module) + " is now tier " + std::to_string(p.tier) + ".");
            return;
        }
        case ProjectKind::JumpGate: {
            int hx = 2;
            systems[p.system].lanes.push_back({p.target, (int16_t)hx});
            systems[p.target].lanes.push_back({p.system, (int16_t)hx});
            rebuildGates();
            clearJumpCache();
            break;
        }
        case ProjectKind::Beacon: markBeacon(*this, p.system); break;
        case ProjectKind::Nexus: {
            Station& o = stations[p.station];
            if (o.dead) break;
            o.nexus = true;
            for (int i = 0; i < ITEM_COUNT; i++) { o.target[i] *= 2.0f; o.prod[i] *= 1.5f; o.cons[i] *= 2.0f; }
            rebuildDerived();
            break;
        }
        case ProjectKind::SurveyArray: systems[p.system].surveyArray = p.founder; break;
        case ProjectKind::ColonyUpgrade: {
            Colony* c = colonyAt(p.station);
            if (!c) break;
            c->cmod[p.module] = (uint8_t)p.tier;
            colonyEconomy(*c);
            addLog(Chan::Corp, Sev::Good, c->owner == player, c->system, stations[c->station].name + ": " + cmodName(p.module) + " now tier " + std::to_string(p.tier) + ".");
            return;
        }
        default: break;
    }
    addLog(Chan::News, Sev::Good, mine, p.system, "The " + p.name + " is complete.");
}

void World::rebuildGates() {
    gateSet.clear();
    gateEnds.clear();
    for (auto& p : projects)
        if (p.kind == ProjectKind::JumpGate && p.stage == ProjectStage::Done) {
            gateSet[pairKey(p.system, p.target)] = p.id;
            gateEnds.push_back({p.system, p.target});
        }
}

Money World::gateToll(const Fleet& f) const { return (Money)20000 * CR * std::max<int64_t>(1, (int64_t)f.ships.size()); }

// Backers travel free; everyone else pays per ship, split among the backers by share.
void World::payToll(Fleet& f, Id from, Id to) {
    auto it = gateSet.find(pairKey(from, to));
    if (it == gateSet.end()) return;
    Project& p = projects[it->second];
    if (p.share.count(f.corp)) return;
    Money toll = gateToll(f);
    if (corps[f.corp].wallet < toll) return;  // the gatekeepers wave the broke through
    pay(f.corp, -toll, Cat::Other, "Gate toll: " + p.name);
    Money total = 0;
    for (auto& [cid, v] : p.share) total += v;
    for (auto& [cid, v] : p.share)
        if (corps[cid].alive) pay(cid, (Money)((double)toll * v / std::max<Money>(1, total)), Cat::Other, "Gate tolls: " + p.name);
    p.tolls += toll;
}

void World::projectsHourly() {
    for (auto& p : projects)
        if (p.stage == ProjectStage::Building && now >= p.buildEnd) completeProject(p);
}

// AI backers: rich corporations near a project pay in, and now and then found one.
void World::projectsDaily() {
    for (auto& p : projects) {
        if (p.stage != ProjectStage::Funding || p.kind == ProjectKind::Upgrade) continue;
        // a project nobody pays into for 120 days is wound up and the credits returned
        if (now - p.founded > 120 * MINUTES_PER_DAY) {
            p.stage = ProjectStage::Cancelled;
            Money total = 0;
            for (auto& [cid, v] : p.share) total += v;
            for (auto& [cid, v] : p.share)
                if (corps[cid].alive) pay(cid, (Money)((double)p.cashGot * 0.8 * v / std::max<Money>(1, total)), Cat::Other, "Wound up: " + p.name);
            addLog(Chan::News, Sev::Warn, p.share.count(player) > 0, p.system, "The " + p.name + " is wound up unfinished; backers get 80% of the credits back.");
            continue;
        }
        Id founder = p.founder;
        bool aiFounded = !corps[founder].player;
        // an AI founder buys its materials in, a little each day
        if (aiFounded && corps[founder].alive)
            for (size_t i = 0; i < p.need.size(); i++) {
                int64_t want = p.need[i].second - p.got[i];
                if (want <= 0) continue;
                int64_t q = std::min<int64_t>(want, std::max<int64_t>(1, p.need[i].second / 25));
                Money cost = item(p.need[i].first).base * q * 12 / 10;
                if (corps[founder].wallet < cost * 3) continue;
                pay(founder, -cost, Cat::Other, "Materials for " + p.name);
                p.got[i] += q;
                p.share[founder] += item(p.need[i].first).base * q;
            }
        // others pay in credits
        Hex at = systems[p.system].hex;
        for (int k = 0; k < 2; k++) {
            if (p.cashGot >= p.cash) break;
            Id cid = (Id)rng.irange(0, (int)corps.size() - 1);
            Corp& c = corps[cid];
            if (c.player || !c.alive || c.wallet < 60000000 * CR) continue;
            if (!(c.arch == Arch::Trader || c.arch == Arch::Industrialist || c.arch == Arch::Miner)) continue;
            if (hexDistance(systems[stations[c.home].system].hex, at) > 30 || !rng.chance(0.25)) continue;
            Money amt = std::min(p.cash - p.cashGot, c.wallet / 40);
            pay(cid, -amt, Cat::Other, "Contribution: " + p.name);
            p.cashGot += amt;
            p.share[cid] += amt;
        }
        if (p.progress() >= 0.9999) {
            p.stage = ProjectStage::Building;
            p.buildEnd = now + (Tick)p.buildDays * MINUTES_PER_DAY;
            addLog(Chan::News, Sev::Good, p.share.count(player) > 0, p.system, p.name + " is fully funded; construction takes " + std::to_string(p.buildDays) + " days.");
        }
    }
    if (rng.chance(1.0 / 25.0)) aiProjects();
    // Deep Survey Arrays find what is hidden near them each fortnight
    if (now % (14 * MINUTES_PER_DAY) < MINUTES_PER_DAY)
        for (auto& s : systems)
            if (s.surveyArray != NONE && corps[s.surveyArray].alive)
                for (Id sy : systemsWithin(s.id, 4))
                    for (const Site& h : hiddenSites(sy, fortnight()))
                        if (!siteKeys.count(h.key)) discoverSite(s.surveyArray, h);
}

// Now and then a rich corporation founds a megaproject; the Charter founds beacons where piracy bites.
void World::aiProjects() {
    int open = 0;
    for (auto& p : projects) open += p.stage <= ProjectStage::Building && p.kind != ProjectKind::Upgrade;
    if (open >= 3 + (int)systems.size() / 400) return;
    Id best = NONE;
    for (auto& c : corps)
        if (c.alive && !c.player && (c.arch == Arch::Trader || c.arch == Arch::Industrialist) && c.wallet > 200000000 * CR &&
            (best == NONE || c.wallet > corps[best].wallet))
            best = c.id;
    if (best == NONE) return;
    Id home = stations[corps[best].home].system;
    // a gate from home to a busy system far off
    if (rng.chance(0.6)) {
        Id far = NONE;
        int bestScore = 0;
        for (Id h : hubs) {
            Id sy = stations[h].system;
            int hx = hexDistance(systems[home].hex, systems[sy].hex);
            if (hx < 12 || hx > 36) continue;
            int sc = hx + rng.irange(0, 10);
            if (sc > bestScore && projectWhy(best, ProjectKind::JumpGate, home, sy, NONE).empty()) { bestScore = sc; far = sy; }
        }
        if (far != NONE) { foundProject(best, ProjectKind::JumpGate, home, far, NONE); return; }
    }
    // a beacon over the worst-hit Contract system near home
    Id worst = NONE;
    for (Id sy : systemsWithin(home, 6))
        if (systems[sy].zone == Zone::Contract && systems[sy].beacon < 2 && (worst == NONE || systems[sy].riskLive > systems[worst].riskLive))
            worst = sy;
    if (worst != NONE && projectWhy(best, ProjectKind::Beacon, worst, NONE, NONE).empty()) foundProject(best, ProjectKind::Beacon, worst, NONE, NONE);
}

}  // namespace nb
