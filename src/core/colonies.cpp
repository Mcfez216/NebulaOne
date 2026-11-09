// NebulaOne - planets and colonies.
//
// Every system has a handful of planets, worked out from the seed when asked
// for. A Colony Kit settles one outside Charter space. The colony is a new
// market (its spaceport): its people buy food, fuel, medicine, electronics and
// luxuries and sell what their world produces. The owner collects taxes and a
// share of the planet's output in goods, delivered to its hangar there. A
// colony that is kept supplied grows; one that is not shrinks and riots.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "world.h"

namespace nb {

const char* planetTypeName(PlanetType t) {
    static const char* n[] = {"Garden", "Ocean", "Rocky", "Ice", "Volcanic", "Barren", "Gas giant"};
    return n[(int)t];
}

const std::vector<std::pair<int, float>>& planetOutputs(PlanetType t) {
    static const std::vector<std::pair<int, float>> out[PLANET_TYPE_COUNT] = {
        {{RATIONS, 0.45f}, {LUXURY_GOODS, 0.35f}, {MEDICAL_KITS, 0.20f}},  // garden
        {{RATIONS, 0.50f}, {MEDICAL_KITS, 0.30f}, {FUEL_CELLS, 0.20f}},    // ocean
        {{FERROX, 0.35f}, {SILVANE, 0.35f}, {CORIEL, 0.30f}},             // rocky
        {{FUEL_CELLS, 0.60f}, {CORIEL, 0.25f}, {SILVANE, 0.15f}},         // ice
        {{VANTIUM, 0.40f}, {LUCENITE, 0.35f}, {ZYRIUM, 0.25f}},           // volcanic
        {{SILVANE, 0.40f}, {VANTIUM, 0.35f}, {AETHERIUM, 0.25f}},         // barren
        {{FUEL_CELLS, 0.70f}, {NOCTURNITE, 0.30f}},                       // gas giant
    };
    return out[(int)t];
}

const char* cmodName(int m) {
    static const char* n[] = {"Habitats", "Extractors", "Spaceport"};
    return (m >= 0 && m < CMOD_COUNT) ? n[m] : "-";
}
const char* cmodEffect(int m) {
    static const char* n[] = {"room for twice the people at tier 1, three times at 2, four at 3",
                              "+50% of the planet's output to you per tier",
                              "a deeper market (+50% per tier) and sensors one jump out"};
    return (m >= 0 && m < CMOD_COUNT) ? n[m] : "";
}

double Colony::cap() const { return 3.0 + 40.0 * size * hab * (1 + cmod[CMOD_HABITAT]); }

static uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
static double unit(uint64_t h) { return (double)(h >> 11) / 9007199254740992.0; }

static int romanValue(const std::string& r) {
    static const char* roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
    for (int k = 0; k < 11; k++)
        if (r == roman[k]) return k + 1;
    return 0;
}

std::vector<Planet> World::planetsOf(Id sys) const {
    static const char* roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
    std::vector<Planet> out;
    if (sys < 0 || sys >= (Id)systems.size()) return out;
    const StarSystem& s = systems[sys];
    uint64_t h = mix(seed ^ ((uint64_t)sys * 0x9E3779B97F4A7C15ull) ^ 0x51A7E7ull);
    int n = 3 + (int)(h % 7);
    // stations are named for the planet they orbit: there are at least that many
    for (Id st : s.stations) {
        const std::string& nm = stations[st].name;
        size_t dash = nm.find(" - ");
        size_t sp = nm.rfind(' ', dash == std::string::npos ? std::string::npos : dash - 1);
        if (dash != std::string::npos && sp != std::string::npos && sp < dash) n = std::max(n, romanValue(nm.substr(sp + 1, dash - sp - 1)));
    }
    n = std::min(n, 11);
    for (int k = 0; k < n; k++) {
        uint64_t hk = mix(h + (uint64_t)k * 0x632BE59BD9B4E019ull);
        double pos = n > 1 ? (double)k / (n - 1) : 0.5, u = unit(hk);
        // inner worlds are scorched, the middle temperate, the outer cold and gassy
        static const int inner[PLANET_TYPE_COUNT] = {2, 3, 25, 0, 35, 35, 0};
        static const int middle[PLANET_TYPE_COUNT] = {12, 20, 30, 10, 10, 18, 0};
        static const int outer[PLANET_TYPE_COUNT] = {0, 0, 5, 35, 0, 15, 45};
        const int* wt = pos < 0.3 ? inner : pos < 0.65 ? middle : outer;
        int total = 0;
        for (int t = 0; t < PLANET_TYPE_COUNT; t++) total += wt[t];
        int roll = (int)(u * total), t = 0;
        while (t < PLANET_TYPE_COUNT - 1 && roll >= wt[t]) roll -= wt[t++];
        Planet p;
        p.index = k;
        p.type = (PlanetType)t;
        double u2 = unit(mix(hk ^ 0xA5)), u3 = unit(mix(hk ^ 0x5A)), u4 = unit(mix(hk ^ 0xC3));
        p.size = p.type == PlanetType::GasGiant ? 4 + (int)(u2 * 2) : p.type == PlanetType::Garden || p.type == PlanetType::Ocean ? 2 + (int)(u2 * 3)
                                                                                                                            : 1 + (int)(u2 * 4);
        static const float lo[PLANET_TYPE_COUNT] = {0.75f, 0.55f, 0.25f, 0.12f, 0.08f, 0.05f, 0.04f};
        static const float hi[PLANET_TYPE_COUNT] = {1.00f, 0.80f, 0.45f, 0.25f, 0.20f, 0.15f, 0.08f};
        p.hab = lo[t] + (float)u3 * (hi[t] - lo[t]);
        p.richness = (float)(0.6 + 0.8 * u4 + 0.2 * s.depth - (s.zone == Zone::Charter ? 0.1 : 0.0));
        p.name = s.name + " " + roman[k];
        out.push_back(p);
    }
    return out;
}

bool World::planetsCharted(Id sys) const {
    if (surveyed.count(sys)) return true;
    for (auto& c : colonies)
        if (c.system == sys && c.owner == player) return true;
    if (systems[sys].owner == player) return true;
    return false;
}

Colony* World::colonyOn(Id sys, int planet) {
    for (auto& c : colonies)
        if (c.system == sys && c.planet == planet && !stations[c.station].dead) return &c;
    return nullptr;
}
Colony* World::colonyAt(Id station) {
    for (auto& c : colonies)
        if (c.station == station) return &c;
    return nullptr;
}
const Colony* World::colonyAt(Id station) const { return const_cast<World*>(this)->colonyAt(station); }

// Per day, per thousand people.
static const int NEED_ITEM[5] = {RATIONS, FUEL_CELLS, MEDICAL_KITS, ELECTRONICS, LUXURY_GOODS};
static const double NEED_RATE[5] = {25.0, 5.0, 2.0, 1.2, 0.5};
static const double NEED_WEIGHT[5] = {0.45, 0.20, 0.15, 0.12, 0.08};

std::vector<std::pair<int, double>> World::colonyNeeds(const Colony& c) const {
    std::vector<std::pair<int, double>> v;
    for (int k = 0; k < 5; k++) v.push_back({NEED_ITEM[k], c.pop * NEED_RATE[k]});
    return v;
}

// The colony's market follows its people: what they eat and what they make.
void World::colonyEconomy(Colony& c) {
    Station& st = stations[c.station];
    for (int i = 0; i < ITEM_COUNT; i++) { st.prod[i] = 0; st.cons[i] = 0; }
    for (int k = 0; k < 5; k++) st.cons[NEED_ITEM[k]] = (float)(c.pop * NEED_RATE[k]);
    double market = c.richness * (40000.0 + 3000.0 * c.pop) * CR;  // what the people sell, per day
    for (auto& [it, share] : planetOutputs(c.type)) st.prod[it] += (float)(market * share / (double)item(it).base);
    // a colony also buys some ore and minerals for its workshops, and a little of everything to keep the spaceport busy
    double depth = 1.0 + 0.5 * c.cmod[CMOD_SPACEPORT];
    for (int i = 0; i < BPO_FIRST; i++) {
        double flow = std::max(st.prod[i], st.cons[i]);
        double T = flow * 5.0 * depth;
        if (T > 0 && T < 3) T = 3;
        st.target[i] = (float)T;
        if (st.stock[i] > T * 4) st.stock[i] = (float)(T * 4);
    }
}

static Id doSettle(World& w, Id corp, Id sys, const Planet& p);

Result World::settleColony(Id corp, Id fid, int planet) {
    if (fid < 0 || fid >= (Id)fleets.size() || !fleets[fid].alive || fleets[fid].corp != corp) return Result::fail("Unknown fleet.");
    Fleet& f = fleets[fid];
    if (f.act == Act::Jump || f.battle != NONE) return Result::fail("The fleet must be in the system, not jumping or fighting.");
    Id sys = f.docked != NONE ? stations[f.docked].system : f.system;
    const StarSystem& s = systems[sys];
    if (s.zone == Zone::Charter) return Result::fail("Charter planets are settled already; colonies go in Contract space or the Frontier.");
    std::vector<Planet> ps = planetsOf(sys);
    if (planet < 0 || planet >= (int)ps.size()) return Result::fail("Pick a planet.");
    if (colonyOn(sys, planet)) return Result::fail(ps[planet].name + " is already settled.");
    auto it = f.cargo.find(COLONY_KIT);
    if (it == f.cargo.end() || it->second <= 0) return Result::fail("The fleet is not carrying a Colony Kit.");
    if (--it->second <= 0) f.cargo.erase(it);
    const Planet& p = ps[planet];
    Id cid = doSettle(*this, corp, sys, p);
    Result r = Result::good(p.name + " settled: 5,000 colonists and a spaceport market. Keep it supplied and it grows.");
    r.id = cid;
    return r;
}

static Id doSettle(World& w, Id corp, Id sys, const Planet& p) {
    auto& colonies = w.colonies;
    auto& corps = w.corps;
    Tick now = w.now;
    int planet = p.index;
    Colony c;
    c.id = (Id)colonies.size();
    c.system = sys;
    c.planet = planet;
    c.type = p.type;
    c.size = p.size;
    c.hab = p.hab;
    c.richness = p.richness;
    c.owner = corp;
    c.pop = 5;
    c.founded = now;
    Id st = w.createStation(sys, StationType::Colony, p.name + " - " + corps[corp].ticker + " Colony");
    Station& S = w.stations[st];
    S.owner = corp;
    S.marketTax = 0.04f;
    S.state = OutpostState::Online;
    c.station = st;
    colonies.push_back(c);
    w.colonyEconomy(colonies.back());
    for (int i = 0; i < ITEM_COUNT; i++) S.stock[i] = S.target[i] * 0.6f;
    S.lastEconomy = now;
    w.markActive(sys);
    if (corps[corp].player) w.surveyed[sys] = std::max(w.surveyed.count(sys) ? w.surveyed[sys] : 0, w.fortnight());
    w.addLog(Chan::News, Sev::Info, corp == w.player, sys, corps[corp].name + " has settled " + p.name + " (" + planetTypeName(p.type) + ").");
    return c.id;
}

Result World::upgradeColony(Id corp, Id cid, int module) {
    if (cid < 0 || cid >= (Id)colonies.size() || module < 0 || module >= CMOD_COUNT) return Result::fail("Unknown upgrade.");
    Colony& c = colonies[cid];
    if (c.owner != corp || stations[c.station].dead) return Result::fail("Not your colony.");
    if (c.cmod[module] >= 3) return Result::fail(std::string(cmodName(module)) + " is at its top tier.");
    for (auto& p : projects)
        if (p.kind == ProjectKind::ColonyUpgrade && p.station == c.station && p.stage <= ProjectStage::Building)
            return Result::fail("One project at a time: " + p.name + " is under way.");
    Project p;
    p.id = (Id)projects.size();
    p.kind = ProjectKind::ColonyUpgrade;
    p.founder = corp;
    p.system = c.system;
    p.station = c.station;
    p.yard = c.station;
    p.module = module;
    p.tier = c.cmod[module] + 1;
    p.need = projectNeeds(ProjectKind::ColonyUpgrade, module, p.tier);
    p.got.assign(p.need.size(), 0);
    p.cash = projectCash(ProjectKind::ColonyUpgrade, module, p.tier);
    p.buildDays = projectDays(ProjectKind::ColonyUpgrade, p.tier);
    p.founded = now;
    p.name = planetsOf(c.system)[c.planet].name + ": " + cmodName(module) + " tier " + std::to_string(p.tier);
    projects.push_back(p);
    Result r = contributeProject(corp, p.id, std::min(corps[corp].wallet, p.cash), true);
    Result out = Result::good(p.name + " started. " + (r.ok ? r.msg : "Bring the materials to the colony's spaceport."));
    out.id = p.id;
    return out;
}

// Hand the colony what it needs from your hangar at its spaceport.
Result World::supplyColony(Id corp, Id cid) {
    if (cid < 0 || cid >= (Id)colonies.size()) return Result::fail("Unknown colony.");
    Colony& c = colonies[cid];
    Station& st = stations[c.station];
    if (st.dead) return Result::fail("The colony is gone.");
    int64_t moved = 0;
    for (int k = 0; k < 5; k++) {
        int it = NEED_ITEM[k];
        int64_t room = (int64_t)std::max(0.0, (double)st.target[it] * 2.0 - st.stock[it]);
        int64_t q = std::min(have(corp, c.station, it), room);
        if (q <= 0) continue;
        takeItem(corp, c.station, it, q);
        st.stock[it] += (float)q;
        moved += q;
    }
    if (!moved) return Result::fail("Nothing it needs in your hangar at " + st.name + ".");
    if (corp != c.owner) addStanding(c.owner, corp, 0.5f);
    return Result::good("Handed " + fmtInt(moved) + " units of supplies to " + st.name + ".");
}

void World::coloniesDaily() {
    for (auto& c : colonies) {
        Station& st = stations[c.station];
        if (st.dead) continue;
        // how well its needs were met
        double sat = 0;
        for (int k = 0; k < 5; k++) {
            int it = NEED_ITEM[k];
            double T = std::max(1.0f, st.target[it]);
            sat += NEED_WEIGHT[k] * std::clamp(st.stock[it] / T, 0.0, 1.0);
        }
        c.sat = (float)sat;
        // growth, or decline
        double cap = c.cap();
        if (sat >= 0.5) c.pop += c.pop * 0.03 * (sat - 0.4) / 0.6 * std::max(0.0, 1.0 - c.pop / cap);
        else c.pop -= c.pop * 0.015 * (0.5 - sat) / 0.5;
        if (c.pop > cap) c.pop -= (c.pop - cap) * 0.1;
        c.pop = std::max(1.0, c.pop);
        if (sat < 0.3) {
            c.unrest += 1;
            if ((int)c.unrest == 7 || (int)c.unrest == 14)
                addLog(Chan::News, Sev::Bad, c.owner == player, c.system, st.name + ": shortages have the colonists in the streets.");
        } else {
            c.unrest = std::max(0.0f, c.unrest - 0.5f);
        }
        c.popHist.push_back((float)c.pop);
        if (c.popHist.size() > 120) c.popHist.erase(c.popHist.begin());
        colonyEconomy(c);
        if (c.owner == NONE || !corps[c.owner].alive) continue;
        // taxes, and the owner's share of the planet's output
        double riot = c.unrest >= 14 ? 0.0 : 1.0;
        Money tax = (Money)(5000.0 * CR * c.pop * sat * riot);
        if (tax > 0) {
            pay(c.owner, tax, Cat::Taxes, "Colony taxes: " + st.name);
            c.taxes += tax;
        }
        double value = c.richness * (80000.0 + 5000.0 * c.pop) * CR * (1.0 + 0.5 * c.cmod[CMOD_EXTRACTOR]) * (0.5 + 0.5 * sat) * riot;
        for (auto& [it, share] : planetOutputs(c.type)) {
            int64_t q = (int64_t)(value * share / (double)item(it).base);
            if (q <= 0) continue;
            addItem(c.owner, c.station, it, q);
            c.output += item(it).base * q;
        }
    }
    if (rng.chance(1.0 / 20.0)) aiColonies();
}

// Rich corporations settle the best worlds near home now and then.
void World::aiColonies() {
    int ai = 0;
    for (auto& c : colonies) ai += !corps[c.owner].player;
    if (ai >= 2 + (int)systems.size() / 300) return;
    std::vector<Id> rich;
    for (auto& k : corps)
        if (k.alive && !k.player && (k.arch == Arch::Industrialist || k.arch == Arch::Trader) && k.wallet > 120000000 * CR) rich.push_back(k.id);
    if (rich.empty()) return;
    Id who = rich[rng.irange(0, (int)rich.size() - 1)];
    Id home = stations[corps[who].home].system;
    double best = 0;
    Id bSys = NONE;
    int bPlanet = -1;
    for (Id sy : systemsWithin(home, 8)) {
        if (systems[sy].zone != Zone::Contract) continue;
        std::vector<Planet> ps = planetsOf(sy);
        for (auto& p : ps) {
            if (colonyOn(sy, p.index)) continue;
            double sc = p.hab * p.size * p.richness * rng.range(0.7, 1.3);
            if (sc > best) { best = sc; bSys = sy; bPlanet = p.index; }
        }
    }
    if (bSys == NONE || best < 1.2) return;
    pay(who, -item(COLONY_KIT).base, Cat::Other, "Colony Kit");
    doSettle(*this, who, bSys, planetsOf(bSys)[bPlanet]);
}

}  // namespace nb
