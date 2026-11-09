// NebulaOne - static item, blueprint, ship and module definitions.
// These tables are game data, not save data: they are rebuilt at startup.
#pragma once
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "types.h"

namespace nb {

enum class ItemCat : uint8_t { Ore, Mineral, Good, Component, Module, Ship, Structure, Blueprint, Copy };
enum class ShipRole : uint8_t { Mining, Hauler, Combat, Scout };

constexpr int ORE_COUNT = 8;
constexpr int MINERAL_COUNT = 8;
constexpr int GOOD_COUNT = 5;
constexpr int COMPONENT_COUNT = 6;
constexpr int MODULE_COUNT = 7;
constexpr int SHIP_COUNT = 7;
constexpr int STRUCTURE_COUNT = 2;
constexpr int BLUEPRINT_COUNT = COMPONENT_COUNT + MODULE_COUNT + SHIP_COUNT + STRUCTURE_COUNT;  // 21

// Item index layout (stable: saves depend on it).
constexpr int ORE_FIRST = 0;
constexpr int MINERAL_FIRST = ORE_FIRST + ORE_COUNT;                  // 8
constexpr int GOOD_FIRST = MINERAL_FIRST + MINERAL_COUNT;             // 16
constexpr int COMPONENT_FIRST = GOOD_FIRST + GOOD_COUNT;              // 21
constexpr int MODULE_FIRST = COMPONENT_FIRST + COMPONENT_COUNT;       // 27
constexpr int SHIP_FIRST = MODULE_FIRST + MODULE_COUNT;               // 34
constexpr int STRUCTURE_FIRST = SHIP_FIRST + SHIP_COUNT;              // 41
constexpr int BPO_FIRST = STRUCTURE_FIRST + STRUCTURE_COUNT;          // 42
constexpr int BPC_FIRST = BPO_FIRST + BLUEPRINT_COUNT;                // 63 (ship copies only)
constexpr int ITEM_COUNT = BPC_FIRST + SHIP_COUNT;                    // 70
constexpr int HANGAR_KEY = 128;  // hangar map key = station * HANGAR_KEY + item

// Blueprint index layout
constexpr int BP_COMPONENT_FIRST = 0;
constexpr int BP_MODULE_FIRST = COMPONENT_COUNT;               // 6
constexpr int BP_SHIP_FIRST = BP_MODULE_FIRST + MODULE_COUNT;  // 13
constexpr int BP_OUTPOST = BP_SHIP_FIRST + SHIP_COUNT;         // 20
constexpr int BP_COLONY = BP_OUTPOST + 1;

enum Item : int {
    CINDRITE = 0, PALLASITE, CORVANE, VANTRITE, LUMINAR, ZYRKON, NOCTYX, AETHERITE,
    FERROX = 8, SILVANE, CORIEL, VANTIUM, LUCENITE, ZYRIUM, NOCTURNITE, AETHERIUM,
    RATIONS = 16, FUEL_CELLS, MEDICAL_KITS, ELECTRONICS, LUXURY_GOODS,
    HULL_PLATING = 21, THRUSTERS, POWER_CORE, SENSOR_ARRAY, WEAPON_MOUNT, EXTRACTION_HEAD,
    MOD_HOLD = 27, MOD_EXTRACTION, MOD_ARMOUR, MOD_DRIVE, MOD_WEAPONS, MOD_EVASION, MOD_SCANNER,
    SHIP_MATTOCK = 34, SHIP_QUARRIER, SHIP_DROVER, SHIP_CARRACK, SHIP_LANCER, SHIP_WARDEN, SHIP_WISP,
    OUTPOST_KIT = 41, COLONY_KIT,
};

enum ShipType : int { MATTOCK = 0, QUARRIER, DROVER, CARRACK, LANCER, WARDEN, WISP };

struct ItemDef {
    std::string name;
    ItemCat cat;
    double volume;   // m3 per unit
    Money base;      // anchor price per unit (cents)
    int depth = 0;   // ores: how far from the core it is common (0 core .. 3 deep Frontier)
    std::array<int, MINERAL_COUNT> refine{};  // ores: minerals per 100 units at perfect yield
    int ship = -1;   // ships and ship copies: ShipDef index
    int bp = -1;     // blueprints and copies: BlueprintDef index; craftable items: the blueprint that makes them
    int module = -1; // modules: ModuleDef index
    std::string desc;
};

struct BlueprintDef {
    int product;     // item made
    int perRun;      // units per run
    std::vector<std::pair<int, int64_t>> materials;  // per run, before research
    int minutes;     // per run, before research
    Money bpoPrice;
    int bpo;         // item index of the original
    int bpc;         // item index of a one-run copy, or -1
    Money materialBase() const;
};

struct ShipDef {
    std::string name;
    std::string cls;
    ShipRole role;
    double cargo;          // m3 general cargo
    double oreHold;        // m3 ore only
    double miningPerHour;  // ore units per hour (pilot skill 50)
    double attack;         // damage per combat round
    double hull;           // hit points
    double minutesPerHex;  // travel time per hex of lane
    int slots;             // fitting slots
    double scan;           // survey and sensor strength (1 = a dedicated survey ship)
    int item;              // item index of the hull
    int bp;                // blueprint index
    std::string blurb;
};

struct ModuleDef {
    std::string effect;  // one-line description
    double cargoMul = 1, miningMul = 1, hullMul = 1, travelMul = 1, attackMul = 1, evasion = 0, scan = 0;
};

const std::vector<ItemDef>& items();
const std::vector<ShipDef>& shipDefs();
const std::vector<BlueprintDef>& blueprints();
const std::vector<ModuleDef>& moduleDefs();
inline const ItemDef& item(int i) { return items()[i]; }
inline const ShipDef& shipDef(int s) { return shipDefs()[s]; }
inline const BlueprintDef& blueprint(int b) { return blueprints()[b]; }
inline const ModuleDef& moduleDef(int m) { return moduleDefs()[m]; }

inline bool isOre(int i) { return i >= ORE_FIRST && i < MINERAL_FIRST; }
inline bool isMineral(int i) { return i >= MINERAL_FIRST && i < GOOD_FIRST; }
inline bool isGood(int i) { return i >= GOOD_FIRST && i < COMPONENT_FIRST; }
inline bool isComponent(int i) { return i >= COMPONENT_FIRST && i < MODULE_FIRST; }
inline bool isModule(int i) { return i >= MODULE_FIRST && i < SHIP_FIRST; }
inline bool isShipItem(int i) { return i >= SHIP_FIRST && i < STRUCTURE_FIRST; }
inline bool isStructure(int i) { return i >= STRUCTURE_FIRST && i < BPO_FIRST; }
inline bool isBpo(int i) { return i >= BPO_FIRST && i < BPC_FIRST; }
inline bool isBpc(int i) { return i >= BPC_FIRST && i < ITEM_COUNT; }
inline bool isBlueprintItem(int i) { return isBpo(i) || isBpc(i); }
inline bool isTradeable(int i) { return i >= 0 && i < ITEM_COUNT; }
const char* catName(ItemCat c);

}  // namespace nb
