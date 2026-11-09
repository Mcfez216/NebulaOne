#include "items.h"

#include <cmath>
#include <cstdio>

namespace nb {

const char* catName(ItemCat c) {
    switch (c) {
        case ItemCat::Ore: return "Ore";
        case ItemCat::Mineral: return "Mineral";
        case ItemCat::Good: return "Trade good";
        case ItemCat::Component: return "Component";
        case ItemCat::Module: return "Module";
        case ItemCat::Ship: return "Ship";
        case ItemCat::Structure: return "Structure";
        case ItemCat::Blueprint: return "Blueprint";
        case ItemCat::Copy: return "Blueprint copy";
    }
    return "?";
}

Money BlueprintDef::materialBase() const {
    Money m = 0;
    for (auto& [it, q] : materials) m += items()[it].base * q;
    return m;
}

// ---------------------------------------------------------------- ships ----
static std::vector<ShipDef> buildShips() {
    std::vector<ShipDef> s(SHIP_COUNT);
    // name, class, role, cargo, ore hold, mining/h, attack, hull, min/hex, slots, scan
    s[MATTOCK] = {"Mattock", "Mining frigate", ShipRole::Mining, 150, 2500, 600, 2, 20, 5.0, 2, 0.15, SHIP_MATTOCK, BP_SHIP_FIRST + MATTOCK,
                  "Cheap, quick, and the first thing most corporations ever own. Two fitting slots."};
    s[QUARRIER] = {"Quarrier", "Mining barge", ShipRole::Mining, 300, 14000, 2600, 3, 60, 8.0, 3, 0.15, SHIP_QUARRIER, BP_SHIP_FIRST + QUARRIER,
                   "A flying ore hold with four extraction heads. Slow, rich and a favourite pirate target."};
    s[DROVER] = {"Drover", "Light hauler", ShipRole::Hauler, 6000, 0, 0, 1, 25, 6.0, 2, 0.1, SHIP_DROVER, BP_SHIP_FIRST + DROVER,
                 "The workhorse of every trade lane. Fit an Expanded Hold and it carries a quarter more."};
    s[CARRACK] = {"Carrack", "Heavy freighter", ShipRole::Hauler, 60000, 0, 0, 4, 120, 11.0, 3, 0.1, SHIP_CARRACK, BP_SHIP_FIRST + CARRACK,
                  "Ten Drovers' worth of hold in one hull. The only ship that can carry an outpost kit."};
    s[LANCER] = {"Lancer", "Escort frigate", ShipRole::Combat, 200, 0, 0, 12, 40, 3.5, 3, 0.3, SHIP_LANCER, BP_SHIP_FIRST + LANCER,
                 "Fast and sharp. Pirates fly them in packs; so do the people who hunt pirates."};
    s[WARDEN] = {"Warden", "Patrol destroyer", ShipRole::Combat, 400, 0, 0, 40, 110, 5.0, 4, 0.3, SHIP_WARDEN, BP_SHIP_FIRST + WARDEN,
                 "Heavy guns and four slots. One Warden on patrol changes a sector's risk rating."};
    s[WISP] = {"Wisp", "Survey frigate", ShipRole::Scout, 400, 0, 0, 1, 18, 3.0, 2, 1.0, SHIP_WISP, BP_SHIP_FIRST + WISP,
               "The fastest hull in the registry, built around a survey array. Finds derelicts, relic caches, hidden ore "
               "and drift lanes. Its sensors reach one jump around it; two with a Survey Scanner."};
    return s;
}

static std::vector<ModuleDef> buildModules() {
    std::vector<ModuleDef> m(MODULE_COUNT);
    m[0] = {"Cargo and ore hold +25%"};
    m[0].cargoMul = 1.25;
    m[1] = {"Mining yield +15%"};
    m[1].miningMul = 1.15;
    m[2] = {"Hull +35%, travel time +8%"};
    m[2].hullMul = 1.35;
    m[2].travelMul = 1.08;
    m[3] = {"Travel time -12%"};
    m[3].travelMul = 0.88;
    m[4] = {"Attack +35%"};
    m[4].attackMul = 1.35;
    m[5] = {"Harder to catch: contact -25%, escape +10 pts"};
    m[5].evasion = 0.25;
    m[6] = {"Survey strength +0.8, sees one jump further"};
    m[6].scan = 0.8;
    return m;
}

// ------------------------------------------------------------ blueprints ---
static std::vector<BlueprintDef> buildBlueprints() {
    std::vector<BlueprintDef> b(BLUEPRINT_COUNT);
    auto x10 = [](std::vector<std::pair<int, int64_t>> v) {
        for (auto& p : v) p.second *= 10;
        return v;
    };
    // components: 10 units per run, half an hour
    b[0] = {HULL_PLATING, 10, x10({{FERROX, 400}, {SILVANE, 60}}), 30, 250000 * CR, 0, -1};
    b[1] = {THRUSTERS, 10, x10({{FERROX, 100}, {CORIEL, 40}, {VANTIUM, 8}}), 30, 300000 * CR, 0, -1};
    b[2] = {POWER_CORE, 10, x10({{VANTIUM, 10}, {LUCENITE, 6}, {ZYRIUM, 1}}), 40, 400000 * CR, 0, -1};
    b[3] = {SENSOR_ARRAY, 10, x10({{SILVANE, 80}, {LUCENITE, 4}, {ELECTRONICS, 1}}), 40, 350000 * CR, 0, -1};
    b[4] = {WEAPON_MOUNT, 10, x10({{FERROX, 200}, {CORIEL, 30}, {VANTIUM, 12}}), 35, 380000 * CR, 0, -1};
    b[5] = {EXTRACTION_HEAD, 10, x10({{FERROX, 150}, {CORIEL, 50}, {LUCENITE, 3}}), 35, 360000 * CR, 0, -1};
    // modules: one per run, an hour
    b[6] = {MOD_HOLD, 1, {{HULL_PLATING, 12}, {THRUSTERS, 2}}, 60, 900000 * CR, 0, -1};
    b[7] = {MOD_EXTRACTION, 1, {{EXTRACTION_HEAD, 12}, {POWER_CORE, 4}}, 60, 1100000 * CR, 0, -1};
    b[8] = {MOD_ARMOUR, 1, {{HULL_PLATING, 20}}, 60, 900000 * CR, 0, -1};
    b[9] = {MOD_DRIVE, 1, {{THRUSTERS, 12}, {POWER_CORE, 4}}, 60, 1000000 * CR, 0, -1};
    b[10] = {MOD_WEAPONS, 1, {{WEAPON_MOUNT, 16}, {POWER_CORE, 4}}, 60, 1200000 * CR, 0, -1};
    b[11] = {MOD_EVASION, 1, {{SENSOR_ARRAY, 12}, {THRUSTERS, 6}}, 60, 1000000 * CR, 0, -1};
    b[12] = {MOD_SCANNER, 1, {{SENSOR_ARRAY, 14}, {POWER_CORE, 3}}, 60, 1100000 * CR, 0, -1};
    // ships
    b[BP_SHIP_FIRST + MATTOCK] = {SHIP_MATTOCK, 1, {{HULL_PLATING, 50}, {THRUSTERS, 20}, {EXTRACTION_HEAD, 25}}, 4 * 60, 1800000 * CR, 0, -1};
    b[BP_SHIP_FIRST + QUARRIER] = {SHIP_QUARRIER, 1, {{HULL_PLATING, 350}, {THRUSTERS, 120}, {POWER_CORE, 80}, {EXTRACTION_HEAD, 200}}, 12 * 60,
                        16000000 * CR, 0, -1};
    b[BP_SHIP_FIRST + DROVER] = {SHIP_DROVER, 1, {{HULL_PLATING, 90}, {THRUSTERS, 40}, {POWER_CORE, 5}}, 5 * 60, 2400000 * CR, 0, -1};
    b[BP_SHIP_FIRST + CARRACK] = {SHIP_CARRACK, 1, {{HULL_PLATING, 1500}, {THRUSTERS, 450}, {POWER_CORE, 300}, {SENSOR_ARRAY, 40}}, 24 * 60,
                       45000000 * CR, 0, -1};
    b[BP_SHIP_FIRST + LANCER] = {SHIP_LANCER, 1, {{HULL_PLATING, 70}, {THRUSTERS, 40}, {WEAPON_MOUNT, 40}, {SENSOR_ARRAY, 10}}, 5 * 60,
                      3000000 * CR, 0, -1};
    b[BP_SHIP_FIRST + WARDEN] = {SHIP_WARDEN, 1, {{HULL_PLATING, 300}, {THRUSTERS, 120}, {POWER_CORE, 60}, {WEAPON_MOUNT, 220}, {SENSOR_ARRAY, 40}},
                      10 * 60, 14000000 * CR, 0, -1};
    b[BP_SHIP_FIRST + WISP] = {SHIP_WISP, 1, {{HULL_PLATING, 40}, {THRUSTERS, 50}, {POWER_CORE, 10}, {SENSOR_ARRAY, 60}}, 5 * 60,
                               3200000 * CR, 0, -1};
    // the outpost kit
    b[BP_OUTPOST] = {OUTPOST_KIT, 1, {{HULL_PLATING, 3000}, {POWER_CORE, 800}, {SENSOR_ARRAY, 200}, {NOCTURNITE, 2000}, {AETHERIUM, 400},
                                      {ELECTRONICS, 1000}}, 48 * 60, 60000000 * CR, 0, -1};
    // the colony kit: habitats, landers and the first settlers' supplies
    b[BP_COLONY] = {COLONY_KIT, 1, {{HULL_PLATING, 1200}, {POWER_CORE, 300}, {SENSOR_ARRAY, 80}, {RATIONS, 6000}, {MEDICAL_KITS, 800},
                                    {ELECTRONICS, 400}}, 36 * 60, 40000000 * CR, 0, -1};
    for (int i = 0; i < BLUEPRINT_COUNT; i++) {
        b[i].bpo = BPO_FIRST + i;
        b[i].bpc = (i >= BP_SHIP_FIRST && i < BP_OUTPOST) ? BPC_FIRST + (i - BP_SHIP_FIRST) : -1;
    }
    return b;
}

// ---------------------------------------------------------------- items ----
static std::vector<ItemDef> buildItems() {
    std::vector<ItemDef> v(ITEM_COUNT);
    auto ore = [&](int i, const char* n, int depth, std::array<int, MINERAL_COUNT> r, const char* d) {
        v[i] = ItemDef{n, ItemCat::Ore, 1.0, 0, depth, r, -1, -1, -1, d};
    };
    // per 100 units at perfect yield:          Fer  Sil  Cor  Van Luc Zyr Noc Aet
    ore(CINDRITE, "Cindrite", 0, {400, 0, 0, 0, 0, 0, 0, 0}, "Dull grey rock, almost pure Ferrox. The bread of every shipyard.");
    ore(PALLASITE, "Pallasite", 0, {250, 120, 0, 0, 0, 0, 0, 0}, "Glassy olive inclusions in iron. Ferrox with a little Silvane.");
    ore(CORVANE, "Corvane", 1, {0, 200, 90, 0, 0, 0, 0, 0}, "Blue-black ore that rings when struck. Silvane and Coriel.");
    ore(VANTRITE, "Vantrite", 1, {0, 0, 150, 40, 0, 0, 0, 0}, "Banded copper ore, prized for Coriel and Vantium.");
    ore(LUMINAR, "Luminar", 2, {0, 0, 0, 60, 25, 0, 0, 0}, "Faintly phosphorescent crystal. Vantium and Lucenite.");
    ore(ZYRKON, "Zyrkon", 2, {0, 0, 0, 0, 40, 12, 0, 0}, "Dense violet ore. Lucenite and the first traces of Zyrium.");
    ore(NOCTYX, "Noctyx", 3, {0, 0, 0, 0, 0, 20, 6, 0}, "Light-swallowing ore from beyond the Charter's reach.");
    ore(AETHERITE, "Aetherite", 3, {0, 0, 0, 0, 0, 0, 8, 3}, "The richest rock known. Worth a fortune, if you live to sell it.");

    const char* minNames[MINERAL_COUNT] = {"Ferrox", "Silvane", "Coriel", "Vantium", "Lucenite", "Zyrium", "Nocturnite", "Aetherium"};
    const double minBase[MINERAL_COUNT] = {5, 12, 30, 90, 250, 700, 2000, 6000};
    const char* minDesc[MINERAL_COUNT] = {
        "Structural metal. Every hull starts here.", "Conductive alloy base for wiring and plating.",
        "Heat-resistant ceramic metal for engines.", "Dense armour-grade metal.",
        "Crystalline lattice used in sensors and shields.", "Exotic conductor for power cores.",
        "Rare dark-matter-bearing mineral. Outposts need it.", "The rarest refined material in known space."};
    for (int m = 0; m < MINERAL_COUNT; m++) {
        v[MINERAL_FIRST + m] = ItemDef{minNames[m], ItemCat::Mineral, 0.01, credits(minBase[m])};
        v[MINERAL_FIRST + m].desc = minDesc[m];
    }
    // Ore anchor price: 40% of its value at perfect yield, so refining (55-75%) usually beats selling raw.
    for (int o = 0; o < ORE_COUNT; o++) {
        double perfect = 0;
        for (int m = 0; m < MINERAL_COUNT; m++) perfect += v[o].refine[m] / 100.0 * minBase[m];
        v[o].base = credits(std::round(perfect * 0.4 * 100.0) / 100.0);
    }
    auto good = [&](int i, const char* n, double vol, double price, const char* d) {
        v[i] = ItemDef{n, ItemCat::Good, vol, credits(price)};
        v[i].desc = d;
    };
    good(RATIONS, "Ration Packs", 0.5, 40, "Sealed meals. Every station eats; mining colonies eat most.");
    good(FUEL_CELLS, "Fuel Cells", 1.0, 120, "Power for stations, garrisons and outposts.");
    good(MEDICAL_KITS, "Medical Kits", 0.2, 300, "Trauma and radiation care. Wanted where people get hurt.");
    good(ELECTRONICS, "Electronics", 0.5, 650, "Processors, sensors and control boards.");
    good(LUXURY_GOODS, "Luxury Goods", 0.3, 1800, "Real coffee, fine textiles, things money cannot print.");

    const auto& bps = blueprints();
    auto matCost = [&](int bp) {
        Money m = 0;
        for (auto& [it, q] : bps[bp].materials) m += v[it].base * q;
        return m / bps[bp].perRun;
    };
    const char* compNames[COMPONENT_COUNT] = {"Hull Plating", "Thruster Assembly", "Power Core", "Sensor Array", "Weapon Mount",
                                              "Extraction Head"};
    const double compVol[COMPONENT_COUNT] = {2.0, 1.5, 1.0, 0.5, 1.5, 1.0};
    const char* compDesc[COMPONENT_COUNT] = {"Pressed Ferrox plate. Every hull needs hundreds.", "Coriel-lined drive unit.",
                                             "Lucenite lattice charged with Zyrium. Power for everything bigger than a frigate.",
                                             "Silvane wiring around Lucenite optics.", "Hardpoint and feed for ship weapons.",
                                             "Cutting head and ore conveyor for mining ships."};
    for (int c = 0; c < COMPONENT_COUNT; c++) {
        int it = COMPONENT_FIRST + c;
        v[it] = ItemDef{compNames[c], ItemCat::Component, compVol[c], 0};
        v[it].bp = BP_COMPONENT_FIRST + c;
        v[it].desc = compDesc[c];
        v[it].base = matCost(v[it].bp) * 112 / 100;
    }
    const char* modNames[MODULE_COUNT] = {"Expanded Hold", "Extraction Upgrade", "Armour Plating", "Drive Tuner", "Weapon Battery",
                                          "Evasion Suite", "Survey Scanner"};
    const auto& mods = moduleDefs();
    for (int m = 0; m < MODULE_COUNT; m++) {
        int it = MODULE_FIRST + m;
        v[it] = ItemDef{modNames[m], ItemCat::Module, 5.0, 0};
        v[it].bp = BP_MODULE_FIRST + m;
        v[it].module = m;
        v[it].desc = "Ship module: " + mods[m].effect + ". Fits any hull with a free slot.";
        v[it].base = matCost(v[it].bp) * 115 / 100;
    }
    const auto& ships = shipDefs();
    for (int s = 0; s < SHIP_COUNT; s++) {
        int it = SHIP_FIRST + s;
        v[it] = ItemDef{ships[s].name, ItemCat::Ship, 0, 0};
        v[it].ship = s;
        v[it].bp = ships[s].bp;
        v[it].desc = ships[s].cls + ". " + ships[s].blurb;
        v[it].base = matCost(ships[s].bp) * 115 / 100;
    }
    v[OUTPOST_KIT] = ItemDef{"Outpost Kit", ItemCat::Structure, 40000, 0};
    v[OUTPOST_KIT].bp = BP_OUTPOST;
    v[OUTPOST_KIT].base = matCost(BP_OUTPOST) * 25 / 10;
    v[OUTPOST_KIT].desc = "A station in a box. Haul it to an empty Contract system or the Frontier with a Carrack and anchor it.";
    v[COLONY_KIT] = ItemDef{"Colony Kit", ItemCat::Structure, 20000, 0};
    v[COLONY_KIT].bp = BP_COLONY;
    v[COLONY_KIT].base = matCost(BP_COLONY) * 2;
    v[COLONY_KIT].desc = "Landers, habitats and a year of supplies. Haul it to a planet outside Charter space and settle it.";
    for (int b = 0; b < BLUEPRINT_COUNT; b++) {
        const BlueprintDef& d = bps[b];
        v[d.bpo] = ItemDef{v[d.product].name + " Blueprint", ItemCat::Blueprint, 0.1, d.bpoPrice};
        v[d.bpo].bp = b;
        v[d.bpo].ship = v[d.product].ship;
        v[d.bpo].desc = "Blueprint original: unlimited runs. Research it at an Institute; copy it to sell runs.";
        if (d.bpc >= 0) {
            v[d.bpc] = ItemDef{v[d.product].name + " Copy", ItemCat::Copy, 0.1, std::max<Money>(CR, d.bpoPrice / 40)};
            v[d.bpc].bp = b;
            v[d.bpc].ship = v[d.product].ship;
            v[d.bpc].desc = "Blueprint copy: one manufacturing run, then it is used up.";
        }
    }
    return v;
}

const std::vector<ShipDef>& shipDefs() {
    static std::vector<ShipDef> s = buildShips();
    return s;
}
const std::vector<ModuleDef>& moduleDefs() {
    static std::vector<ModuleDef> m = buildModules();
    return m;
}
const std::vector<BlueprintDef>& blueprints() {
    static std::vector<BlueprintDef> b = buildBlueprints();
    return b;
}
const std::vector<ItemDef>& items() {
    static std::vector<ItemDef> v = buildItems();
    return v;
}

// ------------------------------------------------------------- formatting --
static std::string groupDigits(uint64_t v) {
    std::string s = std::to_string(v), out;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) {
        out += s[i];
        int rem = n - 1 - i;
        if (rem > 0 && rem % 3 == 0) out += ',';
    }
    return out;
}

std::string fmtInt(int64_t v) {
    if (v < 0) return "-" + groupDigits((uint64_t)(-v));
    return groupDigits((uint64_t)v);
}

std::string fmtMoney(Money m, bool unit) {
    bool neg = m < 0;
    uint64_t a = (uint64_t)(neg ? -m : m);
    char frac[8];
    std::snprintf(frac, sizeof frac, ".%02d", (int)(a % 100));
    std::string s = (neg ? "-" : "") + groupDigits(a / 100) + frac;
    if (unit) s += " cr";
    return s;
}

std::string fmtMoneyShort(Money m, bool unit) {
    double v = toCr(m), a = std::fabs(v);
    char b[48];
    if (a >= 1e12) std::snprintf(b, sizeof b, "%.2fT", v / 1e12);
    else if (a >= 1e9) std::snprintf(b, sizeof b, "%.2fB", v / 1e9);
    else if (a >= 1e6) std::snprintf(b, sizeof b, "%.2fM", v / 1e6);
    else if (a >= 1e4) std::snprintf(b, sizeof b, "%.1fk", v / 1e3);
    else std::snprintf(b, sizeof b, "%.2f", v);
    std::string s = b;
    if (unit) s += " cr";
    return s;
}

std::string fmtQty(double v) {
    char b[32];
    double a = std::fabs(v);
    if (a >= 1e9) std::snprintf(b, sizeof b, "%.2fB", v / 1e9);
    else if (a >= 1e6) std::snprintf(b, sizeof b, "%.2fM", v / 1e6);
    else if (a >= 1e4) std::snprintf(b, sizeof b, "%.1fk", v / 1e3);
    else if (a == std::floor(a)) std::snprintf(b, sizeof b, "%.0f", v);
    else if (a < 1) std::snprintf(b, sizeof b, "%.2f", v);
    else std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// The calendar is deliberately plain: 12 months of 30 days, starting in year 3127.
std::string fmtDate(Tick t) {
    int64_t day = t / MINUTES_PER_DAY;
    int minute = (int)(t % MINUTES_PER_DAY);
    int year = 3127 + (int)(day / 360);
    int doy = (int)(day % 360);
    char b[40];
    std::snprintf(b, sizeof b, "%d.%02d.%02d %02d:%02d", year, doy / 30 + 1, doy % 30 + 1, minute / 60, minute % 60);
    return b;
}

std::string fmtDuration(Tick m) {
    if (m < 0) m = 0;
    char b[40];
    if (m >= MINUTES_PER_DAY) std::snprintf(b, sizeof b, "%lldd %lldh", (long long)(m / 1440), (long long)(m % 1440 / 60));
    else if (m >= 60) std::snprintf(b, sizeof b, "%lldh %02lldm", (long long)(m / 60), (long long)(m % 60));
    else std::snprintf(b, sizeof b, "%lldm", (long long)m);
    return b;
}

}  // namespace nb
