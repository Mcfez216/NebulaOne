// NebulaOne - World: generation of the hex sector chart, scheduling, queries,
// logging and saves.
//
// Everything here is written to scale from the 80-system Compact chart to a
// Galaxy of 8,000+ systems: no structure is N x N, distances come from a
// cached breadth-first search, and routes use A* over the hex geometry.
#include "world.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <numeric>
#include <queue>

#include "archive.h"

namespace nb {

// ------------------------------------------------------------------ labels --
const char* stationTypeName(StationType t) {
    switch (t) {
        case StationType::TradeHub: return "Trade Hub";
        case StationType::Industrial: return "Industrial";
        case StationType::Agricultural: return "Agricultural";
        case StationType::MiningColony: return "Mining Colony";
        case StationType::Research: return "Research";
        case StationType::Military: return "Military";
        case StationType::Outpost: return "Outpost";
        case StationType::Colony: return "Colony";
    }
    return "?";
}
const char* galaxySizeName(GalaxySize g) {
    switch (g) {
        case GalaxySize::Compact: return "Compact";
        case GalaxySize::Standard: return "Standard";
        case GalaxySize::Vast: return "Vast";
        case GalaxySize::Galaxy: return "Galaxy";
        case GalaxySize::Expanse: return "Expanse";
    }
    return "?";
}
int galaxySystemTarget(GalaxySize g) {
    switch (g) {
        case GalaxySize::Compact: return 80;
        case GalaxySize::Standard: return 400;
        case GalaxySize::Vast: return 2000;
        case GalaxySize::Galaxy: return 8000;
        case GalaxySize::Expanse: return 20000;
    }
    return 80;
}
static int aiCorpTarget(GalaxySize g) {
    switch (g) {
        case GalaxySize::Compact: return 12;
        case GalaxySize::Standard: return 40;
        case GalaxySize::Vast: return 120;
        case GalaxySize::Galaxy: return 300;
        case GalaxySize::Expanse: return 600;
    }
    return 12;
}
const char* outpostStateName(OutpostState s) {
    switch (s) {
        case OutpostState::None: return "-";
        case OutpostState::Anchoring: return "Anchoring";
        case OutpostState::Online: return "Online";
        case OutpostState::Offline: return "Offline (no fuel)";
        case OutpostState::Reinforced: return "Reinforced";
    }
    return "?";
}
const char* archName(Arch a) {
    switch (a) {
        case Arch::Player: return "Player";
        case Arch::Trader: return "Trader";
        case Arch::Miner: return "Miner";
        case Arch::Industrialist: return "Industrialist";
        case Arch::Pirate: return "Pirate";
        case Arch::Ganker: return "Ganker";
        case Arch::Mercenary: return "Mercenary";
    }
    return "?";
}
const char* catLabel(Cat c) {
    static const char* n[] = {"Market sales", "Market purchases", "Broker fees", "Sales tax", "Refining fees",
                              "Manufacturing fees", "Wages", "Hiring fees", "Loans received", "Loan repayments",
                              "Loan interest", "Loans made", "Loan repayments received", "Dividends", "Insurance premiums",
                              "Insurance payouts", "Repairs", "Share trades", "Bounties", "Fleet upkeep", "Contracts",
                              "Outpost taxes and fuel", "Research and copying", "Equity raised", "Espionage",
                              "Futures and shorts", "Margin posted and returned", "Bonds issued and redeemed", "Bond coupons and fees",
                              "Underwriting", "Other"};
    return n[(int)c];
}
bool catIsCapital(Cat c) {
    return c == Cat::LoanIn || c == Cat::LendOut || c == Cat::ShareTrade || c == Cat::MarketPurchase || c == Cat::Equity ||
           c == Cat::Margin || c == Cat::Bonds;
}
const char* seatName(int s) {
    static const char* n[] = {"Chief Financial Officer", "Industry Director", "Security Director", "Haulage Director",
                              "Mining Director",         "Trade Director",    "Intelligence Director"};
    return (s >= 0 && s < SEAT_COUNT) ? n[s] : "-";
}
const char* traitName(Trait t) {
    switch (t) {
        case Trait::None: return "-";
        case Trait::Steady: return "Steady";
        case Trait::Reckless: return "Reckless";
        case Trait::Frugal: return "Frugal";
        case Trait::Ambitious: return "Ambitious";
    }
    return "-";
}
const char* orderKindName(OrderKind k) {
    switch (k) {
        case OrderKind::None: return "Standing by";
        case OrderKind::Move: return "Move";
        case OrderKind::Mine: return "Mine";
        case OrderKind::Haul: return "Haul";
        case OrderKind::Trade: return "Trade run";
        case OrderKind::Patrol: return "Patrol";
        case OrderKind::Raid: return "Raid";
        case OrderKind::Siege: return "Siege";
        case OrderKind::Survey: return "Survey";
        case OrderKind::Salvage: return "Salvage";
        case OrderKind::Scout: return "Scout";
    }
    return "?";
}
const char* policyName(RoutePolicy p) {
    switch (p) {
        case RoutePolicy::Shortest: return "Shortest";
        case RoutePolicy::LowestRisk: return "Lowest risk";
        case RoutePolicy::AvoidFrontier: return "Avoid Frontier";
    }
    return "?";
}
const char* actName(Act a) {
    switch (a) {
        case Act::Idle: return "Idle";
        case Act::Undock: return "Undocking";
        case Act::Dock: return "Docking";
        case Act::Jump: return "In transit";
        case Act::ToBelt: return "Moving to belt";
        case Act::Mine: return "Mining";
        case Act::Load: return "Loading";
        case Act::Unload: return "Unloading";
        case Act::Wait: return "Waiting";
        case Act::Combat: return "In combat";
        case Act::Survey: return "Surveying";
        case Act::Salvage: return "Salvaging";
    }
    return "?";
}
const char* jobKindName(JobKind k) {
    switch (k) {
        case JobKind::Refine: return "Refining";
        case JobKind::Manufacture: return "Manufacturing";
        case JobKind::ResearchME: return "Material research";
        case JobKind::ResearchTE: return "Time research";
        case JobKind::Copy: return "Copying";
    }
    return "?";
}
const char* contractKindName(ContractKind k) {
    switch (k) {
        case ContractKind::Supply: return "Supply";
        case ContractKind::Courier: return "Courier";
        case ContractKind::Bounty: return "Bounty";
        case ContractKind::Patrol: return "Patrol";
    }
    return "?";
}
const char* contractStateName(ContractState s) {
    switch (s) {
        case ContractState::Open: return "Open";
        case ContractState::Accepted: return "In progress";
        case ContractState::Done: return "Completed";
        case ContractState::Failed: return "Failed";
        case ContractState::Expired: return "Expired";
        case ContractState::Cancelled: return "Cancelled";
    }
    return "?";
}

// --------------------------------------------------------------- names ------
static const char* SYL_A[] = {"Ka", "Ve", "Ro", "Tha", "An", "Is", "El", "Or", "Dra", "Mi", "Sel", "Tor", "Vy", "Quen", "Ar", "Ul",
                              "Zen", "Pha", "Lo", "Rin", "Cas", "Mor", "Teg", "Ix", "Ny", "Os", "Bel", "Dar", "Ith", "Sar", "Vos",
                              "Eth", "Kor", "Lun", "Hal", "Fen", "Gal", "Sy", "Tem", "Ark", "Ves", "Om", "Iri", "Cal", "Ner", "Ja",
                              "Pel", "Ruk", "Ost", "Hy", "Brae", "Cor", "Ael", "Tiv", "Mal", "Sen", "Thal", "Vir", "Yen", "Zor",
                              "Ad", "Bri", "Cen", "Del", "Esk", "Fal", "Gor", "Hes", "Imr", "Jor", "Kel", "Lir", "Mae", "Nor",
                              "Pyr", "Qua", "Rho", "Sto", "Tan", "Ur", "Val", "Wyn", "Xen", "Yr"};
static const char* SYL_B[] = {"ra", "nis", "ven", "tor", "la", "dis", "mar", "the", "os", "ix", "ane", "eth", "oria", "us", "en",
                              "al", "ion", "ara", "ek", "ith", "ova", "ur", "ell", "an", "is", "ae", "yr", "ond", "ess", "ari",
                              "eon", "ica", "und", "ast", "el", "ora", "ius", "evar", "omi", "ath", "ine", "osk", "uma", "era",
                              "ivo", "ant", "ece", "ilo", "ost", "ure"};
static const char* REGION_SUFFIX[] = {"Reach", "Expanse", "Drift", "March", "Verge", "Hollow", "Deep", "Crown",
                                      "Shoals", "Gulf", "Fringe", "Basin", "Spur", "Rift", "Strand", "Tide"};
static const char* FIRST[] = {"Ada", "Bram", "Chiara", "Dmitri", "Eitan", "Farah", "Goran", "Hana", "Idris", "Jun",
                              "Kaito", "Lena", "Mateo", "Nadia", "Obi", "Priya", "Quinn", "Rafael", "Sana", "Tomas",
                              "Uma", "Viktor", "Wen", "Ximena", "Yusuf", "Zara", "Aiden", "Beatriz", "Cillian", "Dalia",
                              "Emeka", "Freya", "Gideon", "Hyun", "Ines", "Joaquin", "Kiri", "Lior", "Mireille", "Niko",
                              "Odette", "Pavel", "Rosa", "Soren", "Talia", "Ulric", "Valentina", "Wiremu", "Yara", "Zeke",
                              "Anouk", "Bodhi", "Cosima", "Desmond", "Esra", "Florin", "Grace", "Hamid", "Isolde", "Jasper"};
static const char* LAST[] = {"Okafor", "Lindqvist", "Moreau", "Tanaka", "Haddad", "Novak", "Castellanos", "Iyer", "Brennan",
                             "Kowalski", "Mbeki", "Nakamura", "Petrov", "Quaresma", "Rahman", "Sato", "Varga", "Whitlock",
                             "Yilmaz", "Zhang", "Abara", "Bauer", "Carvalho", "Delacroix", "Eriksen", "Fontaine", "Gallo",
                             "Hollis", "Ivanova", "Jensen", "Kader", "Larkin", "Mensah", "Nguyen", "Ortega", "Park",
                             "Rossi", "Sorensen", "Thorne", "Ueda", "Volkov", "Wainwright", "Xu", "Yoon", "Zubiri",
                             "Achterberg", "Blackwood", "Cruz", "Drummond", "Faraday", "Grey", "Hale", "Kirra", "Marsh"};
template <class T, size_t N> static constexpr int countOf(T (&)[N]) { return (int)N; }

std::string World::genName(int style) {
    // style 0: settled system, 2: Frontier designation
    if (style == 2) {
        const char* L = "ABCDEFGHJKLMNPQRSTUVWXYZ";
        const char* LN = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789";
        std::string s;
        s += L[rng.irange(0, 23)];
        s += (char)('0' + rng.irange(0, 9));
        s += '-';
        for (int i = 0; i < 3; i++) s += LN[rng.irange(0, 33)];
        return s;
    }
    std::string s = SYL_A[rng.irange(0, countOf(SYL_A) - 1)];
    s += SYL_B[rng.irange(0, countOf(SYL_B) - 1)];
    double third = systems.size() > 1500 ? 0.6 : 0.25;
    if (rng.chance(third)) s += SYL_B[rng.irange(0, countOf(SYL_B) - 1)];
    return s;
}

std::string World::genPersonName() {
    return std::string(FIRST[rng.irange(0, countOf(FIRST) - 1)]) + " " + LAST[rng.irange(0, countOf(LAST) - 1)];
}

static const char* CORP_WORD[] = {
    "Ashgrove", "Meridian", "Corvid", "Deepvein", "Iron Lantern", "Oberon", "Saltmarsh", "Black Halo", "Red Tithe", "Sable",
    "Vigil", "Halcyon", "Larkspur", "Tallow", "Wexford", "Brightwater", "Cinder", "Gannet", "Hollowmere", "Juniper", "Marrow",
    "Northgate", "Orrin", "Pale Star", "Quillon", "Rook", "Severn", "Thornback", "Umber", "Vantage", "Whitlow", "Yarrow",
    "Zephyr", "Amberline", "Bastion", "Cobalt", "Drake", "Emberfall", "Fairhaven", "Greyward", "Harrow", "Ironside", "Jackal",
    "Keel", "Lodestar", "Mantle", "Nightjar", "Onyx", "Petrel", "Redwater", "Stormglass", "Tidewater", "Verdigris", "Wolfram",
    "Arclight", "Blackwater", "Coldharbour", "Dunmore", "Eastwind", "Fathom", "Gallowglass", "Highmark", "Ironbark", "Kingfisher",
    "Longreach", "Mirefield", "Nettle", "Oakheart", "Pikestaff", "Quicksilver", "Ravensworth", "Saltire", "Tern", "Undertow",
    "Vespertine", "Wyvern", "Ashfall", "Brass Key", "Copperline", "Dawnward", "Elder Star", "Flint", "Glasswing", "Heron",
    "Inkwell", "Jetty", "Kestrel", "Lantern Hill", "Millrace", "Nightingale", "Orchid", "Pennant", "Quartermile", "Rimeward"};

std::string World::genCorpName(Arch a, std::string* ticker) {
    static const char* trader[] = {"Mercantile", "Trading Company", "Haulage", "Exchange", "Freight", "Commerce Guild"};
    static const char* miner[] = {"Extraction", "Mining", "Ore Works", "Minerals", "Deepcore", "Prospectors"};
    static const char* ind[] = {"Heavy Industries", "Shipwrights", "Foundry", "Fabrication", "Works", "Yards"};
    static const char* pirate[] = {"Syndicate", "Brotherhood", "Raiders", "Black Flag", "Corsairs", "Reavers"};
    static const char* ganker[] = {"Hawks", "Jackals", "Gutter Rats", "Wolves", "Crows", "Knives"};
    static const char* merc[] = {"Arms Company", "Concord", "Free Company", "Security", "Legion", "Guard"};
    const char** suf = trader;
    int ns = 6;
    switch (a) {
        case Arch::Miner: suf = miner; break;
        case Arch::Industrialist: suf = ind; break;
        case Arch::Pirate: suf = pirate; break;
        case Arch::Ganker: suf = ganker; break;
        case Arch::Mercenary: suf = merc; break;
        default: break;
    }
    std::string name;
    for (int k = 0; k < 200; k++) {
        std::string w = CORP_WORD[rng.irange(0, countOf(CORP_WORD) - 1)];
        std::string s = suf[rng.irange(0, ns - 1)];
        name = a == Arch::Ganker ? "The " + w + " " + s : w + " " + s;
        if (k > 150) name += " " + std::to_string(rng.irange(2, 99));
        bool used = false;
        for (auto& c : corps) used = used || c.name == name;
        if (!used) break;
    }
    // ticker: initials and consonants, four letters, unique
    std::string letters;
    for (char ch : name)
        if (std::isalpha((unsigned char)ch)) letters += (char)std::toupper((unsigned char)ch);
    std::string t;
    for (int k = 0; k < 400; k++) {
        t.clear();
        if (k == 0) {
            t = letters.substr(0, 4);
        } else {
            t += letters[0];
            for (int j = 0; j < 3; j++) t += letters[rng.irange(1, std::max(1, (int)letters.size() - 1))];
        }
        while (t.size() < 4) t += 'X';
        bool used = false;
        for (auto& c : corps) used = used || c.ticker == t;
        if (!used) break;
    }
    if (ticker) *ticker = t;
    return name;
}

// -------------------------------------------------------------- hex helpers --
static int64_t ckey(int q, int r) { return (int64_t)(q + 32768) * 65536 + (r + 32768); }
static const int HEX_DIR[6][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};
static const float SQRT3 = 1.7320508f;
static void hexToMap(int q, int r, float& x, float& y) {
    x = 15.0f * q;
    y = 10.0f * SQRT3 * (r + q * 0.5f);
}

int World::cellAt(int q, int r) const {
    auto it = cellIndex.find(ckey(q, r));
    return it == cellIndex.end() ? -1 : it->second;
}

// Smooth value noise in [0,1], from the seed (independent of the rng stream).
static double hash01(uint64_t seed, int x, int y) {
    uint64_t h = seed ^ ((uint64_t)(uint32_t)x * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)y * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}
static double valueNoise(uint64_t seed, double x, double y) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    double fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    double a = hash01(seed, x0, y0), b = hash01(seed, x0 + 1, y0), c = hash01(seed, x0, y0 + 1), d = hash01(seed, x0 + 1, y0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

static bool segCross(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    auto orient = [](float px, float py, float qx, float qy, float rx, float ry) {
        return (qx - px) * (ry - py) - (qy - py) * (rx - px);
    };
    float o1 = orient(ax, ay, bx, by, cx, cy), o2 = orient(ax, ay, bx, by, dx, dy);
    float o3 = orient(cx, cy, dx, dy, ax, ay), o4 = orient(cx, cy, dx, dy, bx, by);
    return (o1 * o2 < 0) && (o3 * o4 < 0);
}

// -------------------------------------------------------------- generation --
void World::genUniverse(const NewGameSetup& setup) {
    const int N = galaxySystemTarget(setup.size);
    const uint64_t nseed = seed * 0x2545F4914F6CDD1Dull + 77;
    // ---- the chart: a ragged disc of hex cells, about five per system
    double M = N * 5.0 / 0.82;
    int R = std::max(6, (int)std::ceil((-3.0 + std::sqrt(12.0 * M - 3.0)) / 6.0));
    hexRadius = R;
    double ph1 = rng.range(0, 6.283), ph2 = rng.range(0, 6.283);
    cells.clear();
    cellIndex.clear();
    for (int q = -R; q <= R; q++)
        for (int r = std::max(-R, -q - R); r <= std::min(R, -q + R); r++) {
            int d = (std::abs(q) + std::abs(r) + std::abs(q + r)) / 2;
            float x, y;
            hexToMap(q, r, x, y);
            double th = std::atan2(y, x);
            double edge = 1.0 + 0.10 * std::sin(3 * th + ph1) + 0.06 * std::sin(5 * th + ph2);
            if ((double)d / R > edge * 0.97) continue;
            HexCell c;
            c.q = (int16_t)q;
            c.r = (int16_t)r;
            cellIndex[ckey(q, r)] = (int)cells.size();
            cells.push_back(c);
        }
    // ---- systems: never on neighbouring cells, denser in some places than others
    std::vector<int> order(cells.size());
    std::iota(order.begin(), order.end(), 0);
    for (int i = (int)order.size() - 1; i > 0; i--) std::swap(order[i], order[rng.irange(0, i)]);
    std::vector<char> blocked(cells.size(), 0);
    systems.clear();
    auto place = [&](int ci) {
        HexCell& c = cells[ci];
        StarSystem s;
        s.id = (Id)systems.size();
        s.hex = {c.q, c.r};
        hexToMap(c.q, c.r, s.x, s.y);
        c.system = s.id;
        systems.push_back(std::move(s));
        blocked[ci] = 1;
        for (auto& d : HEX_DIR) {
            int n = cellAt(c.q + d[0], c.r + d[1]);
            if (n >= 0) blocked[n] = 1;
        }
    };
    double nscale = 1.0 / 7.0;
    for (int pass = 0; pass < 2 && (int)systems.size() < N; pass++)
        for (int ci : order) {
            if ((int)systems.size() >= N) break;
            if (blocked[ci]) continue;
            if (pass == 0) {
                double n = valueNoise(nseed, cells[ci].q * nscale, cells[ci].r * nscale);
                if (!rng.chance(0.15 + 0.85 * n)) continue;
            }
            place(ci);
        }
    const int n = (int)systems.size();

    // ---- the Charter: one or more core worlds, law thinning out with distance
    int K = setup.size == GalaxySize::Compact ? 1 : setup.size == GalaxySize::Standard ? 3 : setup.size == GalaxySize::Vast ? 8 : setup.size == GalaxySize::Galaxy ? 24 : 56;
    std::vector<Id> cores;
    {
        Id c0 = 0;
        int best = 1 << 30;
        for (auto& s : systems) {
            int d = hexDistance(s.hex, {0, 0});
            if (d < best) { best = d; c0 = s.id; }
        }
        cores.push_back(c0);
        for (int k = 1; k < K; k++) {
            Id pick = NONE;
            int pickD = -1;
            for (int t = 0; t < 120; t++) {
                Id cand = (Id)rng.irange(0, n - 1);
                if (hexDistance(systems[cand].hex, {0, 0}) > R * 0.72) continue;
                int md = 1 << 30;
                for (Id c : cores) md = std::min(md, hexDistance(systems[cand].hex, systems[c].hex));
                if (md > pickD) { pickD = md; pick = cand; }
            }
            if (pick != NONE) cores.push_back(pick);
        }
    }
    std::vector<std::pair<double, int>> rank(n);
    for (int i = 0; i < n; i++) {
        int md = 1 << 30;
        for (Id c : cores) md = std::min(md, hexDistance(systems[i].hex, systems[c].hex));
        double wob = valueNoise(nseed + 5, systems[i].hex.q / 9.0, systems[i].hex.r / 9.0) - 0.5;
        rank[i] = {md * (1.0 + 0.35 * wob) + rng.normal(0, 0.8), i};
    }
    std::sort(rank.begin(), rank.end());
    for (int k = 0; k < n; k++) {
        StarSystem& s = systems[rank[k].second];
        double q = (k + 0.5) / n;
        if (q < 0.15) { s.zone = Zone::Charter; s.depth = 0; }
        else if (q < 0.30) { s.zone = Zone::Charter; s.depth = 1; }
        else if (q < 0.47) { s.zone = Zone::Contract; s.depth = 1; }
        else if (q < 0.65) { s.zone = Zone::Contract; s.depth = 2; }
        else { s.zone = Zone::Frontier; s.depth = 3; }
    }
    // every empty cell takes the zone of its nearest system, so the chart reads as territory
    {
        std::vector<int> q;
        std::vector<char> seen(cells.size(), 0);
        for (size_t i = 0; i < cells.size(); i++)
            if (cells[i].system != NONE) { cells[i].zone = systems[cells[i].system].zone; seen[i] = 1; q.push_back((int)i); }
        for (size_t h = 0; h < q.size(); h++) {
            const HexCell& c = cells[q[h]];
            for (auto& d : HEX_DIR) {
                int nb2 = cellAt(c.q + d[0], c.r + d[1]);
                if (nb2 >= 0 && !seen[nb2]) { seen[nb2] = 1; cells[nb2].zone = c.zone; q.push_back(nb2); }
            }
        }
    }

    // ---- regions: grown outward over the chart from scattered seeds
    int nreg = std::max(4, n / 22);
    double spacing = std::sqrt((double)cells.size() / nreg) * 0.85;
    std::vector<Id> seeds;
    for (int t = 0; t < nreg * 40 && (int)seeds.size() < nreg; t++) {
        Id cand = (Id)rng.irange(0, n - 1);
        bool ok = true;
        for (Id s2 : seeds)
            if (hexDistance(systems[cand].hex, systems[s2].hex) < spacing) { ok = false; break; }
        if (ok) seeds.push_back(cand);
    }
    {
        std::vector<int> q;
        q.reserve(cells.size());
        for (size_t k = 0; k < seeds.size(); k++) {
            int ci = cellAt(systems[seeds[k]].hex.q, systems[seeds[k]].hex.r);
            cells[ci].region = (Id)k;
            q.push_back(ci);
        }
        for (size_t h = 0; h < q.size(); h++) {
            const HexCell& c = cells[q[h]];
            for (auto& d : HEX_DIR) {
                int nb2 = cellAt(c.q + d[0], c.r + d[1]);
                if (nb2 >= 0 && cells[nb2].region == NONE) { cells[nb2].region = c.region; q.push_back(nb2); }
            }
        }
    }
    regions.assign(seeds.size(), Region{});
    std::set<std::string> used;
    auto uniq = [&](int style) {
        for (int k = 0; k < 60; k++) {
            std::string s = genName(style);
            if (!used.count(s)) { used.insert(s); return s; }
        }
        for (int k = 2; k < 999; k++) {
            std::string s = genName(style) + " " + std::to_string(k);
            if (!used.count(s)) { used.insert(s); return s; }
        }
        return genName(style);
    };
    std::vector<std::array<int, 3>> zoneCount(regions.size(), {0, 0, 0});
    std::vector<int> regCount(regions.size(), 0);
    for (auto& s : systems) {
        int ci = cellAt(s.hex.q, s.hex.r);
        s.region = cells[ci].region == NONE ? 0 : cells[ci].region;
        zoneCount[s.region][(int)s.zone]++;
        regions[s.region].cx += s.x;
        regions[s.region].cy += s.y;
        regCount[s.region]++;
    }
    for (size_t r = 0; r < regions.size(); r++) {
        auto& z = zoneCount[r];
        regions[r].zone = z[0] >= z[1] && z[0] >= z[2] ? Zone::Charter : z[1] >= z[2] ? Zone::Contract : Zone::Frontier;
        std::string base = uniq(0);
        regions[r].name = base + " " + REGION_SUFFIX[rng.irange(0, countOf(REGION_SUFFIX) - 1)];
        if (regCount[r]) { regions[r].cx /= regCount[r]; regions[r].cy /= regCount[r]; }
    }
    // names and designations: wargame-style column/row
    int digits = R * 2 + 1 >= 100 ? 3 : 2;
    for (auto& s : systems) {
        s.name = s.zone == Zone::Frontier ? uniq(2) : uniq(0);
        int col = s.hex.q + R, row = s.hex.r + (s.hex.q - (s.hex.q & 1)) / 2 + R;
        char b[32];
        if (digits == 2) std::snprintf(b, sizeof b, "%02d%02d", col, row);
        else std::snprintf(b, sizeof b, "%03d.%03d", col, row);
        s.hexId = b;
    }

    // ---- lanes: a spanning tree over near neighbours, bridges between islands, then extra short lanes
    struct E { int len; uint32_t tie; Id a, b; };
    std::vector<E> edges;
    const int LR = 4;
    for (auto& s : systems)
        for (int dq = -LR; dq <= LR; dq++)
            for (int dr = std::max(-LR, -dq - LR); dr <= std::min(LR, -dq + LR); dr++) {
                int ci = cellAt(s.hex.q + dq, s.hex.r + dr);
                if (ci < 0) continue;
                Id b = cells[ci].system;
                if (b == NONE || b <= s.id) continue;
                edges.push_back({hexDistance(s.hex, systems[b].hex), (uint32_t)(rng.next() >> 40), s.id, b});
            }
    std::sort(edges.begin(), edges.end(), [](const E& x, const E& y) { return x.len != y.len ? x.len < y.len : x.tie < y.tie; });
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto linked = [&](Id a, Id b) {
        for (auto& l : systems[a].lanes)
            if (l.to == b) return true;
        return false;
    };
    auto link = [&](Id a, Id b) {
        int16_t h = (int16_t)hexDistance(systems[a].hex, systems[b].hex);
        systems[a].lanes.push_back({b, h});
        systems[b].lanes.push_back({a, h});
    };
    for (auto& e : edges) {
        int ra = find(e.a), rb = find(e.b);
        if (ra != rb) { parent[ra] = rb; link(e.a, e.b); }
    }
    // bridge disconnected islands to their nearest neighbour
    for (int guard = 0; guard < 4096; guard++) {
        std::map<int, int> sizes;
        for (int i = 0; i < n; i++) sizes[find(i)]++;
        if (sizes.size() <= 1) break;
        int bigRoot = -1, bigN = -1;
        for (auto& [rt, c] : sizes)
            if (c > bigN) { bigN = c; bigRoot = rt; }
        int smallRoot = -1, smallN = 1 << 30;
        for (auto& [rt, c] : sizes)
            if (rt != bigRoot && c < smallN) { smallN = c; smallRoot = rt; }
        int bestD = 1 << 30;
        Id ba = NONE, bb = NONE;
        for (int i = 0; i < n; i++) {
            if (find(i) != smallRoot) continue;
            for (int rad = 1; rad <= 12 && bestD > rad; rad++) {
                // walk the ring at distance rad
                int q = systems[i].hex.q + HEX_DIR[4][0] * rad, r = systems[i].hex.r + HEX_DIR[4][1] * rad;
                for (int side = 0; side < 6; side++)
                    for (int st = 0; st < rad; st++) {
                        int ci = cellAt(q, r);
                        if (ci >= 0 && cells[ci].system != NONE && find(cells[ci].system) != smallRoot && rad < bestD) {
                            bestD = rad; ba = i; bb = cells[ci].system;
                        }
                        q += HEX_DIR[side][0];
                        r += HEX_DIR[side][1];
                    }
            }
        }
        if (ba == NONE) {  // far apart: brute force
            for (int i = 0; i < n; i++) {
                if (find(i) != smallRoot) continue;
                for (int j = 0; j < n; j++) {
                    if (find(j) == smallRoot) continue;
                    int d = hexDistance(systems[i].hex, systems[j].hex);
                    if (d < bestD) { bestD = d; ba = i; bb = j; }
                }
            }
        }
        if (ba == NONE) break;
        link(ba, bb);
        parent[find(ba)] = find(bb);
    }
    // extra short lanes make loops; fewer across zone borders, so borders have chokepoints
    for (auto& e : edges) {
        if (e.len > 3) break;
        if (linked(e.a, e.b)) continue;
        if (systems[e.a].lanes.size() >= 4 || systems[e.b].lanes.size() >= 4) continue;
        Zone za = systems[e.a].zone, zb = systems[e.b].zone;
        double p = za != zb ? 0.18 : za == Zone::Frontier ? 0.38 : 0.55;
        if (!rng.chance(p)) continue;
        const StarSystem& A = systems[e.a];
        const StarSystem& B = systems[e.b];
        bool cross = false;
        const int CR2 = 6;
        for (int dq = -CR2; dq <= CR2 && !cross; dq++)
            for (int dr = std::max(-CR2, -dq - CR2); dr <= std::min(CR2, -dq + CR2) && !cross; dr++) {
                int ci = cellAt(A.hex.q + dq, A.hex.r + dr);
                if (ci < 0 || cells[ci].system == NONE) continue;
                const StarSystem& C = systems[cells[ci].system];
                if (C.id == A.id || C.id == B.id) continue;
                for (auto& l : C.lanes) {
                    if (l.to == A.id || l.to == B.id || l.to < C.id) continue;
                    const StarSystem& D = systems[l.to];
                    if (segCross(A.x, A.y, B.x, B.y, C.x, C.y, D.x, D.y)) { cross = true; break; }
                }
            }
        if (!cross) link(e.a, e.b);
    }

    // ---- belts: deeper space, richer rock
    for (auto& s : systems) {
        int d = s.depth;
        int nb = rng.irange(1 + (d > 0 ? 1 : 0), 3 + (d >= 2 ? 1 : 0));
        for (int b = 0; b < nb; b++) {
            double w[ORE_COUNT] = {};
            double tot = 0;
            for (int o = 0; o < ORE_COUNT; o++) {
                int od = item(o).depth;
                w[o] = od == d ? 3.0 : od == d - 1 ? 1.5 : od < d - 1 ? 0.6 : od == d + 1 ? 0.25 : 0.0;
                tot += w[o];
            }
            double pick = rng.uniform() * tot;
            int ore = 0;
            for (int o = 0; o < ORE_COUNT; o++) {
                pick -= w[o];
                if (pick <= 0 && w[o] > 0) { ore = o; break; }
            }
            Belt bl;
            bl.ore = ore;
            bl.capacity = std::round(rng.range(300000, 900000) * (1.0 + 0.3 * d) / 1000.0) * 1000.0;
            bl.remaining = bl.capacity * rng.range(0.75, 1.0);
            s.belts.push_back(bl);
        }
        // starting risk: the base rating for the zone and depth
        s.risk = s.riskLive = s.zone == Zone::Charter ? 8.0f + 3.0f * d : s.zone == Zone::Contract ? 35.0f + 5.0f * d : 70.0f;
    }
}

Id World::createStation(Id system, StationType t, const std::string& name) {
    Station st;
    st.id = (Id)stations.size();
    st.system = system;
    st.type = t;
    st.name = name;
    st.stock.assign(ITEM_COUNT, 0);
    st.target.assign(ITEM_COUNT, 0);
    st.prod.assign(ITEM_COUNT, 0);
    st.cons.assign(ITEM_COUNT, 0);
    st.eventMul.assign(ITEM_COUNT, 1.0f);
    st.bookOf.assign(ITEM_COUNT, -1);
    st.lastEconomy = now;
    systems[system].stations.push_back(st.id);
    stations.push_back(std::move(st));
    return (Id)stations.size() - 1;
}

// Population demand and output for one station, by type, zone and depth.
// Goods producers are scaled in genStations so the whole chart roughly balances.
void World::setupEconomy(Station& st) {
    const StarSystem& sys = systems[st.system];
    int t = (int)st.type;
    static const double goodCons[GOOD_COUNT] = {500, 200, 40, 25, 8};
    static const double typeMul[STATION_TYPE_COUNT][GOOD_COUNT] = {
        {1.5, 1.2, 1.0, 2.0, 2.0},  // hub
        {1.0, 1.5, 0.8, 0.6, 0.5},  // industrial
        {0.3, 1.0, 0.8, 0.5, 0.5},  // agricultural
        {2.2, 1.2, 1.6, 0.5, 0.8},  // mining colony
        {0.8, 0.8, 0.5, 1.6, 1.2},  // research
        {1.5, 3.0, 2.0, 2.0, 0.6},  // military
        {1.2, 2.5, 1.5, 0.8, 1.0},  // outpost
    };
    double zoneMul = sys.zone == Zone::Charter ? (sys.depth == 0 ? 1.0 : 1.1) : sys.zone == Zone::Contract ? 1.4 : 1.6;
    for (int g = 0; g < GOOD_COUNT; g++) {
        double c = goodCons[g] * typeMul[t][g] * zoneMul * rng.range(0.85, 1.15);
        if (sys.zone != Zone::Charter && g == 4) c *= 2.0;
        if (sys.zone != Zone::Charter && g == 2) c *= 1.5;
        st.cons[GOOD_FIRST + g] = (float)c;
    }
    static const double indMin[MINERAL_COUNT] = {15000, 4500, 1200, 250, 50, 9, 2, 0.6};
    static const double milMin[MINERAL_COUNT] = {0, 0, 0, 200, 75, 20, 5, 1.5};
    static const double hubMin[MINERAL_COUNT] = {4000, 1200, 350, 70, 20, 5, 1, 0.5};
    static const double opMin[MINERAL_COUNT] = {0, 0, 0, 0, 30, 12, 6, 2};
    for (int m = 0; m < MINERAL_COUNT; m++) {
        double c = 0;
        if (st.type == StationType::Industrial) c = indMin[m];
        if (st.type == StationType::Military) c = milMin[m];
        if (st.type == StationType::TradeHub) c = hubMin[m];
        if (st.type == StationType::Outpost) c = opMin[m];
        st.cons[MINERAL_FIRST + m] = (float)(c * rng.range(0.85, 1.15));
    }
    if (st.type == StationType::MiningColony) {
        static const double d0[MINERAL_COUNT] = {13000, 3000, 0, 0, 0, 0, 0, 0};
        static const double d1[MINERAL_COUNT] = {3000, 4500, 1800, 240, 0, 0, 0, 0};
        static const double d2[MINERAL_COUNT] = {0, 0, 600, 450, 160, 32, 5, 0.8};
        const double* src = sys.depth == 0 ? d0 : sys.depth == 1 ? d1 : d2;
        for (int m = 0; m < MINERAL_COUNT; m++) st.prod[MINERAL_FIRST + m] = (float)(src[m] * rng.range(0.8, 1.2));
    }
    for (int o = 0; o < ORE_COUNT; o++) {
        double c = 0;
        int od = item(o).depth;
        if (st.type == StationType::MiningColony && od <= sys.depth) c = 15000.0 / (1 + od);
        if (st.type == StationType::Industrial && od <= 1) c = 12000.0;
        if (st.type == StationType::TradeHub) c = 6000.0 / (1 + od);
        if (st.type == StationType::Outpost) c = 9000.0 / (1 + od);
        st.cons[o] = (float)c;
    }
    // components: shipyards eat them; foundries make them (scaled in genStations)
    static const double compFlow[COMPONENT_COUNT] = {40, 15, 8, 4, 10, 8};
    for (int c = 0; c < COMPONENT_COUNT; c++) {
        double v = 0;
        if (st.type == StationType::Industrial) v = compFlow[c];
        if (st.type == StationType::TradeHub) v = compFlow[c] * 0.25;
        if (st.type == StationType::Military && (c == 0 || c == 4)) v = compFlow[c] * 0.4;
        if (st.type == StationType::Outpost && (c == 0 || c == 2)) v = compFlow[c] * 0.2;
        st.cons[COMPONENT_FIRST + c] = (float)(v * rng.range(0.85, 1.15));
    }
    static const double modFlow[MODULE_COUNT] = {0.5, 0.3, 0.6, 0.5, 0.6, 0.5, 0.3};
    for (int m = 0; m < MODULE_COUNT; m++) {
        double v = 0;
        if (st.type == StationType::Military) v = modFlow[m];
        if (st.type == StationType::TradeHub) v = modFlow[m] * 0.6;
        if (st.type == StationType::Outpost) v = modFlow[m] * 0.4;
        st.cons[MODULE_FIRST + m] = (float)v;
    }
    if (st.type == StationType::TradeHub || st.type == StationType::Industrial) {
        for (int s = 0; s < SHIP_COUNT; s++) {
            double r = (s == CARRACK || s == WARDEN || s == QUARRIER) ? 0.25 : 0.5;
            if (st.type == StationType::Industrial) r *= 0.6;
            st.prod[SHIP_FIRST + s] = (float)r;
            st.cons[SHIP_FIRST + s] = (float)r;
        }
    }
    if (st.type == StationType::TradeHub) {
        if (sys.zone == Zone::Charter)
            for (int s = 0; s < SHIP_COUNT; s++) st.prod[BPC_FIRST + s] = st.cons[BPC_FIRST + s] = 0.6f;
        if (sys.zone == Zone::Contract) st.prod[OUTPOST_KIT] = st.cons[OUTPOST_KIT] = 0.08f;
        if (sys.zone == Zone::Charter) st.prod[COLONY_KIT] = st.cons[COLONY_KIT] = 0.1f;
    }
}

static void finishTargets(World& w, Station& st) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        double flow = std::max(st.prod[i], st.cons[i]);
        double days = isGood(i) || isMineral(i) || isComponent(i) ? 5.0 : isOre(i) ? 3.0 : 8.0;
        double T = flow * days;
        if ((isShipItem(i) || isModule(i) || isBpc(i)) && T > 0) T = std::max(3.0, std::round(T));
        if ((i == OUTPOST_KIT || i == COLONY_KIT) && T > 0) T = 2;
        st.target[i] = (float)T;
        double S = T * w.rng.range(0.7, 1.3);
        if (isShipItem(i) || isModule(i) || isBpc(i) || isStructure(i)) S = std::round(S);
        st.stock[i] = (float)S;
    }
    if (st.type == StationType::TradeHub && w.systems[st.system].zone == Zone::Charter)
        for (int b = BPO_FIRST; b < BPC_FIRST; b++) st.target[b] = st.stock[b] = 1000;
}

void World::genStations() {
    static const char* label[] = {"Exchange", "Foundry", "Agri Dome", "Ore Depot", "Institute", "Garrison", "Outpost"};
    static const char* roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
    auto make = [&](StarSystem& s, StationType t) {
        createStation(s.id, t, s.name + " " + roman[rng.irange(0, 10)] + " - " + label[(int)t]);
    };
    // a trade hub in each region that has lawful space: its best-connected Charter system, else Contract
    regionHub.assign(regions.size(), NONE);
    std::vector<Id> hubSys(regions.size(), NONE);
    std::vector<double> hubScore(regions.size(), -1e9);
    for (auto& s : systems) {
        if (s.zone == Zone::Frontier) continue;
        const Region& rg = regions[s.region];
        double dx = s.x - rg.cx, dy = s.y - rg.cy;
        double score = (s.zone == Zone::Charter ? 100.0 : 0.0) + 6.0 * s.lanes.size() - std::sqrt(dx * dx + dy * dy) / 15.0;
        if (score > hubScore[s.region]) { hubScore[s.region] = score; hubSys[s.region] = s.id; }
    }
    for (size_t r = 0; r < regions.size(); r++)
        if (hubSys[r] != NONE) make(systems[hubSys[r]], StationType::TradeHub);
    for (auto& s : systems) {
        int count = 0;
        if (s.zone == Zone::Charter) count = rng.irange(1, 3);
        else if (s.zone == Zone::Contract) count = rng.chance(0.7) ? rng.irange(1, 2) : 0;
        if (hubSys[s.region] == s.id) count = std::max(0, count - 1);
        for (int k = 0; k < count; k++) {
            // Industrial, Agricultural, MiningColony, Research, Military
            double w[5] = {24, 24, 22, 14, 10};
            if (s.zone == Zone::Contract) { w[0] = 12; w[1] = 14; w[2] = 38; w[3] = 8; w[4] = 28; }
            if (s.zone == Zone::Charter && s.depth == 1) { w[2] = 28; w[4] = 14; }
            double tot = 0;
            for (double x : w) tot += x;
            double p = rng.uniform() * tot;
            int t = 0;
            for (; t < 4; t++) { p -= w[t]; if (p <= 0) break; }
            make(s, (StationType)(t + 1));
        }
    }
    for (auto& st : stations) setupEconomy(st);
    // scale goods, component and module producers so supply roughly meets demand everywhere
    static const double goodShare[STATION_TYPE_COUNT][GOOD_COUNT] = {
        {0, 0, 0, 0, 1.0}, {0, 1.0, 0, 0.6, 0}, {1.0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 1.0, 0.4, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};
    double totG[GOOD_COUNT] = {}, shG[GOOD_COUNT] = {}, totC[COMPONENT_COUNT] = {}, totM[MODULE_COUNT] = {};
    int nInd = 0;
    for (auto& st : stations) {
        for (int g = 0; g < GOOD_COUNT; g++) { totG[g] += st.cons[GOOD_FIRST + g]; shG[g] += goodShare[(int)st.type][g]; }
        for (int c = 0; c < COMPONENT_COUNT; c++) totC[c] += st.cons[COMPONENT_FIRST + c];
        for (int m = 0; m < MODULE_COUNT; m++) totM[m] += st.cons[MODULE_FIRST + m];
        nInd += st.type == StationType::Industrial;
    }
    for (auto& st : stations) {
        for (int g = 0; g < GOOD_COUNT; g++)
            if (shG[g] > 0) st.prod[GOOD_FIRST + g] = (float)(totG[g] * 1.12 * goodShare[(int)st.type][g] / shG[g]);
        if (st.type == StationType::Industrial && nInd > 0) {
            for (int c = 0; c < COMPONENT_COUNT; c++) st.prod[COMPONENT_FIRST + c] = (float)(totC[c] * 1.04 / nInd);
            for (int m = 0; m < MODULE_COUNT; m++) st.prod[MODULE_FIRST + m] = (float)(totM[m] * 1.0 / nInd);
        }
        finishTargets(*this, st);
    }
    hubs.clear();
    for (auto& st : stations)
        if (st.type == StationType::TradeHub) {
            hubs.push_back(st.id);
            regionHub[systems[st.system].region] = st.id;
        }
}

Id World::genPeople(Id hub, int pilots, int execs) {
    Id last = NONE;
    for (int k = 0; k < pilots + execs; k++) {
        Person p;
        p.name = genPersonName();
        p.role = k < pilots ? Role::Pilot : Role::Executive;
        auto skill = [&] { return std::clamp((int)std::round(rng.normal(50, 17)), 8, 98); };
        p.mining = skill();
        p.nav = skill();
        p.gunnery = skill();
        p.mgmt = skill();
        p.trait = (Trait)rng.irange(0, 4);
        if (p.role == Role::Pilot) {
            int best = std::max({p.mining, p.nav, p.gunnery});
            p.wage = credits(2000 + best * 60.0);
        } else {
            p.seat = rng.irange(0, SEAT_COUNT - 1);
            p.wage = credits(15000 + p.mgmt * 250.0);
        }
        p.loyalty = (float)rng.range(55, 85);
        p.ambition = p.trait == Trait::Ambitious ? rng.irange(70, 95) : p.trait == Trait::Frugal ? rng.irange(15, 40) : rng.irange(30, 65);
        if (p.trait == Trait::Frugal) p.wage = p.wage * 85 / 100;
        if (p.trait == Trait::Ambitious) p.wage = p.wage * 120 / 100;
        p.wage = p.wage / 1000 * 1000;
        p.hub = hub;
        p.availableUntil = now + 7 * MINUTES_PER_DAY;
        // reuse a slot left by someone who left the game's view, so the list never grows without bound
        Id slot = NONE;
        for (auto& q : people)
            if (!q.alive && q.employer == NONE && q.ship == NONE) { slot = q.id; break; }
        if (slot == NONE) { p.id = (Id)people.size(); people.push_back(p); }
        else { p.id = slot; people[slot] = p; }
        last = p.id;
    }
    return last;
}

std::vector<Id> World::recruitHubs() const {
    // the hub nearest home and the five next nearest, plus hubs where the player keeps ships
    std::vector<std::pair<int, Id>> d;
    Id homeSys = stations[pc().home].system;
    const auto& row = jumpsFrom(homeSys);
    for (Id h : hubs) d.push_back({row[stations[h].system], h});
    std::sort(d.begin(), d.end());
    std::vector<Id> out;
    for (size_t i = 0; i < d.size() && i < 6; i++) out.push_back(d[i].second);
    for (Id o : outpostIds)  // the player's outposts with a big enough exchange draw job seekers too
        if (!stations[o].dead && stations[o].owner == player && stations[o].smod[SMOD_MARKET] >= 2 && out.size() < 12 &&
            std::find(out.begin(), out.end(), o) == out.end())
            out.push_back(o);
    for (Id s : pc().shipList) {
        if (!ships[s].alive || ships[s].fleet != NONE) continue;
        Id st = ships[s].station;
        if (stations[st].type == StationType::TradeHub && std::find(out.begin(), out.end(), st) == out.end() && out.size() < 12)
            out.push_back(st);
    }
    return out;
}

Id World::createCorp(Arch a, const std::string& name, const std::string& ticker, Id home, double walletM) {
    Corp c;
    c.id = (Id)corps.size();
    c.name = name;
    c.ticker = ticker;
    c.arch = a;
    c.home = home;
    c.wallet = credits(walletM * 1e6);
    c.founded = now;
    c.livery = (uint8_t)((uint32_t)corps.size() * 2654435761u % 8);
    corps.push_back(std::move(c));
    return (Id)corps.size() - 1;
}

void World::genCorps(const NewGameSetup& s) {
    corps.clear();
    // ---- the player, at the trade hub of the first Charter core
    Id home = hubs.empty() ? 0 : hubs[0];
    {
        Id c0sys = NONE;
        int best = 1 << 30;
        for (auto& sy : systems)
            if (sy.zone == Zone::Charter && sy.depth == 0) {
                int d = hexDistance(sy.hex, {0, 0});
                if (d < best) { best = d; c0sys = sy.id; }
            }
        if (c0sys != NONE) {
            Id rh = regionHub[systems[c0sys].region];
            if (rh != NONE && systems[stations[rh].system].zone == Zone::Charter) home = rh;
            else {
                int bd = 1 << 30;
                for (Id h : hubs) {
                    if (systems[stations[h].system].zone != Zone::Charter) continue;
                    int d = hexDistance(systems[stations[h].system].hex, systems[c0sys].hex);
                    if (d < bd) { bd = d; home = h; }
                }
            }
        }
    }
    player = createCorp(Arch::Player, s.corpName, s.ticker, home, 1.5);
    Corp& p = corps[player];
    p.player = true;
    p.livery = (uint8_t)std::clamp(s.livery, 0, 7);
    p.rating = 2;
    p.sharesOut = 10000000;
    p.charterStanding = 0;

    // ---- AI corporations: a mix of archetypes spread over the chart
    int nAI = aiCorpTarget(s.size);
    std::vector<Arch> mix;
    auto addN = [&](Arch a, int k) { for (int i = 0; i < k; i++) mix.push_back(a); };
    int nt = (int)std::lround(nAI * 0.25), nm = (int)std::lround(nAI * 0.20), ni = (int)std::lround(nAI * 0.15),
        np = (int)std::lround(nAI * 0.15), ng = std::max(1, (int)std::lround(nAI * 0.07));
    addN(Arch::Trader, nt);
    addN(Arch::Miner, nm);
    addN(Arch::Industrialist, ni);
    addN(Arch::Pirate, np);
    addN(Arch::Ganker, ng);
    addN(Arch::Mercenary, std::max(1, nAI - nt - nm - ni - np - ng));
    auto stationsWhere = [&](std::function<bool(const Station&, const StarSystem&)> pred) {
        std::vector<Id> v;
        for (auto& st : stations)
            if (pred(st, systems[st.system])) v.push_back(st.id);
        return v;
    };
    std::vector<Id> cTrader = hubs;
    std::vector<Id> cMinerSafe = stationsWhere([](const Station& st, const StarSystem& sy) {
        return st.type == StationType::MiningColony && sy.zone == Zone::Charter;
    });
    std::vector<Id> cMinerDeep = stationsWhere([](const Station& st, const StarSystem& sy) {
        return st.type == StationType::MiningColony && sy.zone == Zone::Contract;
    });
    std::vector<Id> cInd = stationsWhere([](const Station& st, const StarSystem& sy) {
        return st.type == StationType::Industrial && sy.zone == Zone::Charter;
    });
    std::vector<Id> cPirate = stationsWhere([](const Station&, const StarSystem& sy) { return sy.zone == Zone::Contract && sy.depth == 2; });
    std::vector<Id> cGanker = stationsWhere([](const Station&, const StarSystem& sy) { return sy.zone == Zone::Contract && sy.depth == 1; });
    std::vector<Id> cMerc = stationsWhere([](const Station& st, const StarSystem& sy) {
        return st.type == StationType::Military && sy.zone == Zone::Contract;
    });
    auto pick = [&](std::vector<Id>& v, Id fallback) {
        if (v.empty()) return fallback;
        int k = rng.irange(0, (int)v.size() - 1);
        Id r = v[k];
        if (v.size() > 3) { v[k] = v.back(); v.pop_back(); }  // spread corporations out while there is room
        return r;
    };
    for (size_t k = 0; k < mix.size(); k++) {
        Arch a = mix[k];
        std::string ticker;
        std::string name = genCorpName(a, &ticker);
        Id h = home;
        double wM = 30;
        switch (a) {
            case Arch::Trader: h = pick(cTrader, home); wM = rng.range(45, 70); break;
            case Arch::Miner: h = (k % 2 == 0) ? pick(cMinerSafe, home) : pick(cMinerDeep.empty() ? cMinerSafe : cMinerDeep, home); wM = rng.range(26, 32); break;
            case Arch::Industrialist: h = pick(cInd, home); wM = rng.range(110, 140); break;
            case Arch::Pirate: h = pick(cPirate.empty() ? cGanker : cPirate, home); wM = rng.range(18, 22); break;
            case Arch::Ganker: h = pick(cGanker, home); wM = rng.range(28, 32); break;
            case Arch::Mercenary: h = pick(cMerc.empty() ? cGanker : cMerc, home); wM = rng.range(32, 40); break;
            default: break;
        }
        Id id = createCorp(a, name, ticker, h, wM);
        Corp& c = corps[id];
        c.aiSkill = (float)rng.range(42, 60);
        c.nextThink = rng.irange(5, 120);
        switch (a) {
            case Arch::Trader: case Arch::Miner: case Arch::Industrialist: c.rating = 1; c.charterStanding = 3; break;
            case Arch::Pirate: c.rating = 4; c.charterStanding = -8; c.aiSkill = 60; break;
            case Arch::Ganker: c.rating = 4; c.charterStanding = -6; c.aiSkill = 56; break;
            case Arch::Mercenary: c.rating = 2; c.charterStanding = 5; c.aiSkill = 64; break;
            default: break;
        }
    }
    // ---- starting hulls
    auto give = [&](Id c, int def, int k) {
        for (int i = 0; i < k; i++) spawnShip(c, def, corps[c].home);
    };
    give(player, MATTOCK, 1);
    give(player, DROVER, 1);
    for (auto& c : corps) {
        if (c.player) continue;
        int v = rng.irange(0, 2);
        switch (c.arch) {
            case Arch::Trader:
                if (v == 0) { give(c.id, DROVER, 3); give(c.id, CARRACK, 1); }
                else if (v == 1) give(c.id, DROVER, 4);
                else { give(c.id, DROVER, 2); give(c.id, CARRACK, 2); }
                break;
            case Arch::Miner:
                if (v == 0) { give(c.id, QUARRIER, 5); give(c.id, MATTOCK, 3); }
                else if (v == 1) { give(c.id, QUARRIER, 4); give(c.id, MATTOCK, 2); give(c.id, LANCER, 3); }
                else { give(c.id, QUARRIER, 3); give(c.id, MATTOCK, 4); }
                break;
            case Arch::Industrialist:
                if (v == 0) give(c.id, DROVER, 2); else { give(c.id, DROVER, 1); give(c.id, CARRACK, 1); }
                break;
            case Arch::Pirate:
                if (v == 0) { give(c.id, LANCER, 4); give(c.id, WARDEN, 2); }
                else if (v == 1) { give(c.id, LANCER, 4); give(c.id, WARDEN, 1); }
                else give(c.id, LANCER, 6);
                break;
            case Arch::Ganker: give(c.id, LANCER, 5); break;
            case Arch::Mercenary:
                if (v == 0) { give(c.id, WARDEN, 3); give(c.id, LANCER, 3); }
                else { give(c.id, WARDEN, 2); give(c.id, LANCER, 4); }
                break;
            default: break;
        }
        if (c.arch == Arch::Industrialist) {
            for (int b = BP_COMPONENT_FIRST; b < BP_MODULE_FIRST; b++) addItem(c.id, c.home, BPO_FIRST + b, 1);
            std::vector<int> shipBps = {MATTOCK, DROVER, CARRACK, LANCER, QUARRIER, WARDEN};
            for (int i = (int)shipBps.size() - 1; i > 0; i--) std::swap(shipBps[i], shipBps[rng.irange(0, i)]);
            int keep = rng.irange(4, 5);
            for (int i = 0; i < keep; i++) addItem(c.id, c.home, BPO_FIRST + BP_SHIP_FIRST + shipBps[i], 1);
            addItem(c.id, c.home, BPO_FIRST + BP_MODULE_FIRST + rng.irange(0, MODULE_COUNT - 1), 1);
            for (int b = 0; b < BLUEPRINT_COUNT; b++) { c.me[b] = (uint8_t)rng.irange(2, 6); c.te[b] = (uint8_t)rng.irange(1, 4); }
        }
    }
    // ---- a few corporations already hold outposts out in the Frontier (bigger charts only)
    int nOut = (int)systems.size() / 160;
    std::vector<Id> owners;
    for (auto& c : corps)
        if (!c.player && (c.arch == Arch::Pirate || c.arch == Arch::Mercenary || c.arch == Arch::Miner)) owners.push_back(c.id);
    for (int k = 0; k < nOut && !owners.empty(); k++) {
        // a Frontier system one or two lanes beyond the Contract border
        Id sys = NONE;
        for (int t = 0; t < 200 && sys == NONE; t++) {
            Id cand = (Id)rng.irange(0, (int)systems.size() - 1);
            const StarSystem& sy = systems[cand];
            if (sy.zone != Zone::Frontier || sy.owner != NONE || !sy.stations.empty()) continue;
            bool nearBorder = false;
            for (auto& l : sy.lanes)
                for (auto& l2 : systems[l.to].lanes) nearBorder = nearBorder || systems[l2.to].zone == Zone::Contract || systems[l.to].zone == Zone::Contract;
            if (nearBorder) sys = cand;
        }
        if (sys == NONE) continue;
        Id owner = owners[rng.irange(0, (int)owners.size() - 1)];
        Id st = createStation(sys, StationType::Outpost, systems[sys].name + " - " + corps[owner].ticker + " Outpost");
        Station& o = stations[st];
        o.owner = owner;
        o.state = OutpostState::Online;
        o.marketTax = 0.06f;
        o.refineTax = 0.06f;
        o.shield = o.hull = 1.0;
        setupEconomy(o);
        finishTargets(*this, o);
        systems[sys].owner = owner;
        addItem(owner, st, FUEL_CELLS, 6000);
        if (corps[owner].arch == Arch::Pirate) corps[owner].home = st;
    }
    for (auto& c : corps)
        if (!c.player) aiFormFleets(c);

    // ---- the player's first two pilots, and recruits at nearby hubs
    for (int k = 0; k < 2; k++) {
        Person& pp = people[genPeople(home, 1, 0)];
        pp.employer = player;
        pp.hub = NONE;
        pp.mining = 45 + k * 8;
        pp.nav = 50;
        pp.gunnery = 40;
        pp.wage = credits(4500);
    }
    for (Id h : recruitHubs()) genPeople(h, 6, 2);
}

void World::generate(const NewGameSetup& s) {
    *this = World();
    seed = s.seed;
    size = s.size;
    fog = s.fog;
    rng.seed(seed * 0x9E3779B97F4A7C15ull + 12345);
    now = 0;
    genUniverse(s);
    genStations();
    rebuildDerived();
    genCorps(s);
    rebuildDerived();
    // stations around the player and every hub open their books, with a short price history
    Id homeSys = stations[pc().home].system;
    std::vector<char> nearHome(systems.size(), 0);
    for (Id sy : systemsWithin(homeSys, 4)) nearHome[sy] = 1;
    for (auto& st : stations) {
        bool hub = st.type == StationType::TradeHub;
        if (!hub && !nearHome[st.system]) continue;
        st.activeUntil = now + 6 * MINUTES_PER_HOUR;
        makerRepost(st);
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (st.target[i] <= 0) continue;
            Book& bk = st.book(i);
            double p = toCr(refPrice(st.id, i));
            double walk = 1.0;
            for (int d = -20; d < 0; d++) {
                walk *= std::exp(rng.normal(0, 0.025));
                walk = std::clamp(walk, 0.85, 1.15);
                Bar b;
                b.day = d;
                b.open = (float)(p * walk);
                b.close = (float)(p * walk * std::exp(rng.normal(0, 0.012)));
                b.high = std::max(b.open, b.close) * 1.015f;
                b.low = std::min(b.open, b.close) * 0.985f;
                b.volume = (float)(std::max(st.prod[i], st.cons[i]) * rng.range(0.2, 0.6));
                bk.hist.push_back(b);
            }
            bk.last = credits(bk.hist.back().close);
            bk.lastCorpTradeDay = -1;
        }
    }
    for (auto& c : corps) {
        c.netWorth = computeNetWorth(c.id);
        c.sharePrice = std::max<Money>(CR, c.netWorth / c.sharesOut);
        c.profitPeriod = c.netWorth;
        for (int d = 0; d < 20; d++) c.shareHist.push_back((Money)(c.sharePrice * rng.range(0.94, 1.06)));
        c.shareHist.push_back(c.sharePrice);
        c.stats.push_back({0, c.wallet, c.netWorth});
    }
    pstats.bestNetWorth = pc().netWorth;
    riskDaily();
    for (auto& sy : systems) sy.risk = sy.riskLive;
    contractsDaily();
    // what the player knows on day one: its own neighbourhood and the Charter Exchange's close
    updateSight();
    intelDaily();
    for (size_t sy = 0; sy < systems.size(); sy++)
        if (seen((Id)sy))
            for (Id st : systems[sy].stations) snapshotStation(st);
    addLog(Chan::Corp, Sev::Good, true, homeSys, pc().name + " is chartered at " + stations[pc().home].name + ". The board expects growth.");
    addLog(Chan::News, Sev::Info, false, NONE,
           "The Charter Authority reminds all captains: its Wardens answer violence in Charter space only. Beyond the Charter, "
           "check the underwriters' risk rating before you fly.");
}

void World::rebuildDerived() {
    cellIndex.clear();
    for (size_t i = 0; i < cells.size(); i++) cellIndex[ckey(cells[i].q, cells[i].r)] = (int)i;
    hubs.clear();
    outpostIds.clear();
    regionHub.assign(regions.size(), NONE);
    regionSystems.assign(regions.size(), {});
    for (auto& sy : systems)
        if (sy.region != NONE) regionSystems[sy.region].push_back(sy.id);
    for (auto& st : stations) {
        if (st.type == StationType::TradeHub && !st.dead) {
            hubs.push_back(st.id);
            regionHub[systems[st.system].region] = st.id;
        }
        if (st.isOutpost()) outpostIds.push_back(st.id);
        if (st.nexus && !st.dead) hubs.push_back(st.id);
    }
    rebuildGates();
    for (auto& c : corps) { c.shipList.clear(); c.fleetList.clear(); c.orderSpots.clear(); }
    for (auto& s : ships)
        if (s.alive) corps[s.corp].shipList.push_back(s.id);
    for (auto& f : fleets)
        if (f.alive) corps[f.corp].fleetList.push_back(f.id);
    for (auto& st : stations)
        for (auto& bk : st.books) {
            for (auto& o : bk.sells)
                if (o.corp != NONE) corps[o.corp].orderSpots.insert(hkey(st.id, bk.item));
            for (auto& o : bk.buys)
                if (o.corp != NONE) corps[o.corp].orderSpots.insert(hkey(st.id, bk.item));
        }
    jumpCache_.clear();
    rebuildFleetIndex();
    if (regionReveal.size() != regions.size()) regionReveal.assign(regions.size(), 0);
    siteKeys.clear();
    for (auto& st : sites) siteKeys[st.key] = 1;
    rebuildEventIndex();
    rebuildDrift();
    rebuildPacts();
    updateSight();
}

void World::rebuildFleetIndex() {
    fleetsIn.assign(systems.size(), {});
    for (auto& f : fleets)
        if (f.alive && f.docked == NONE) fleetsIn[f.system].push_back(f.id);
}

// --------------------------------------------------------------- routing ----
const std::vector<int16_t>& World::jumpsFrom(Id sys) const {
    auto it = jumpCache_.find(sys);
    if (it != jumpCache_.end()) {
        it->second.second = ++jumpClock_;
        return it->second.first;
    }
    if (jumpCache_.size() >= 384) {
        auto oldest = jumpCache_.begin();
        for (auto i2 = jumpCache_.begin(); i2 != jumpCache_.end(); ++i2)
            if (i2->second.second < oldest->second.second) oldest = i2;
        jumpCache_.erase(oldest);
    }
    std::vector<int16_t> d(systems.size(), 999);
    std::vector<Id> q;
    q.reserve(systems.size());
    d[sys] = 0;
    q.push_back(sys);
    for (size_t h = 0; h < q.size(); h++) {
        Id u = q[h];
        for (auto& l : systems[u].lanes)
            if (d[l.to] == 999) { d[l.to] = d[u] + 1; q.push_back(l.to); }
    }
    auto& slot = jumpCache_[sys];
    slot.first = std::move(d);
    slot.second = ++jumpClock_;
    return slot.first;
}

int World::jumps(Id a, Id b) const {
    if (a == NONE || b == NONE) return 999;
    if (a == b) return 0;
    // prefer a row that is already cached
    auto ia = jumpCache_.find(a);
    if (ia != jumpCache_.end()) return ia->second.first[b];
    auto ib = jumpCache_.find(b);
    if (ib != jumpCache_.end()) return ib->second.first[a];
    return jumpsFrom(a)[b];
}

std::vector<Id> World::systemsWithin(Id sys, int maxJumps, std::vector<int>* dist) const {
    std::vector<Id> out;
    if (dist) dist->clear();
    if (sys == NONE) return out;
    std::vector<int> d;
    std::unordered_map<Id, char> seen;
    out.push_back(sys);
    d.push_back(0);
    seen[sys] = 1;
    for (size_t h = 0; h < out.size(); h++) {
        int du = d[h];
        if (du >= maxJumps) continue;
        for (auto& l : systems[out[h]].lanes)
            if (seen.emplace(l.to, 1).second) { out.push_back(l.to); d.push_back(du + 1); }
    }
    if (dist) *dist = std::move(d);
    return out;
}

int World::laneHexes(Id a, Id b) const {
    for (auto& l : systems[a].lanes)
        if (l.to == b) return l.hexes;
    for (auto& e : drift)
        if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return e.hexes;
    return std::max(1, hexDistance(systems[a].hex, systems[b].hex));
}

// A* over the lanes. Costs are lane lengths in hexes, scaled up for risk;
// the straight hex distance never overestimates, so the route is optimal.
std::vector<Id> World::route(Id from, Id to, RoutePolicy p, Id corp) const {
    std::vector<Id> out;
    if (from == to || from == NONE || to == NONE) return out;
    // a corporation's own drift lanes are shortcuts; with them the hex heuristic no longer holds
    std::vector<const DriftEdge*> mine;
    if (corp != NONE)
        for (auto& e : drift)
            if (e.corp == corp) mine.push_back(&e);
    const double hk = mine.empty() ? 1.0 : 0.0;
    // a jump gate is a short lane between far systems: the straight-line estimate must allow for it
    auto hdist = [&](Id u, Hex goal_) {
        int d = hexDistance(systems[u].hex, goal_);
        for (auto& [ga, gb] : gateEnds) {
            int via1 = hexDistance(systems[u].hex, systems[ga].hex) + 2 + hexDistance(systems[gb].hex, goal_);
            int via2 = hexDistance(systems[u].hex, systems[gb].hex) + 2 + hexDistance(systems[ga].hex, goal_);
            d = std::min({d, via1, via2});
        }
        return d;
    };
    std::unordered_map<Id, double> dist;
    std::unordered_map<Id, Id> prev;
    using QE = std::pair<double, Id>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    Hex goal = systems[to].hex;
    dist[from] = 0;
    pq.push({hk * hdist(from, goal), from});
    auto relax = [&](Id u, double du, Id v, double w) {
        if (v != to) {
            const StarSystem& sv = systems[v];
            if (p == RoutePolicy::LowestRisk) w *= 1.0 + sv.risk / 12.0;
            if (p == RoutePolicy::AvoidFrontier && sv.zone == Zone::Frontier) w *= 20.0;
            if (stormBlocked(v)) w *= 40.0;  // go round an ion storm if there is a way
        }
        double nd = du + w;
        auto it = dist.find(v);
        if (it == dist.end() || nd < it->second - 1e-9) {
            dist[v] = nd;
            prev[v] = u;
            pq.push({nd + hk * hdist(v, goal), v});
        }
    };
    while (!pq.empty()) {
        auto [f, u] = pq.top();
        pq.pop();
        if (u == to) break;
        double du = dist[u];
        if (f - hk * hdist(u, goal) > du + 1e-9) continue;
        for (auto& l : systems[u].lanes) relax(u, du, l.to, l.hexes);
        for (const DriftEdge* e : mine) {
            if (e->a == u) relax(u, du, e->b, e->hexes);
            else if (e->b == u) relax(u, du, e->a, e->hexes);
        }
    }
    if (!prev.count(to)) return out;
    for (Id v = to; v != from; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return out;
}

Id World::nearestStation(Id system, bool) const {
    // look outward ring by ring: cheap even on the largest charts
    std::vector<int> dist;
    std::vector<Id> ring = systemsWithin(system, 12, &dist);
    for (size_t k = 0; k < ring.size(); k++)
        for (Id st : systems[ring[k]].stations)
            if (stationUsable(st)) return st;
    Id best = NONE;
    int bd = 1 << 30;
    for (auto& st : stations)
        if (stationUsable(st.id)) {
            int d = hexDistance(systems[system].hex, systems[st.system].hex);
            if (d < bd) { bd = d; best = st.id; }
        }
    return best;
}

bool World::stationUsable(Id st) const {
    if (st < 0 || st >= (Id)stations.size()) return false;
    const Station& s = stations[st];
    if (s.dead) return false;
    if (s.isOutpost()) return s.state == OutpostState::Online || s.state == OutpostState::Reinforced || s.state == OutpostState::Offline;
    return true;
}

// --------------------------------------------------------------- queries ----
float World::defaultStanding(Id a, Id b) const {
    if (a == b) return 10;
    Arch A = corps[a].arch, B = corps[b].arch;
    bool oa = A == Arch::Pirate || A == Arch::Ganker, ob = B == Arch::Pirate || B == Arch::Ganker;
    if ((A == Arch::Mercenary && ob) || (B == Arch::Mercenary && oa)) return -10;
    if (oa && ob) return 2;
    if (oa || ob) return -4;
    return 0;
}
float World::standing(Id a, Id b) const {
    auto it = corps[a].standing.find(b);
    return it == corps[a].standing.end() ? defaultStanding(a, b) : it->second;
}
void World::addStanding(Id a, Id b, float d) {
    if (a == NONE || b == NONE || a == b) return;
    float v = std::clamp(standing(a, b) + d, -10.0f, 10.0f);
    corps[a].standing[b] = v;
}

int64_t World::have(Id corp, Id station, int it) const {
    if (isShipItem(it)) {
        int64_t n = 0;
        int def = it - SHIP_FIRST;
        for (Id sid : corps[corp].shipList) {
            const Ship& s = ships[sid];
            if (s.alive && s.corp == corp && s.fleet == NONE && s.station == station && s.def == def) n++;
        }
        return n;
    }
    auto& h = corps[corp].hangar;
    auto itr = h.find(hkey(station, it));
    return itr == h.end() ? 0 : itr->second;
}

double World::hangarVolume(Id corp, Id station) const {
    double v = 0;
    auto& h = corps[corp].hangar;
    for (auto it = h.lower_bound(hkey(station, 0)); it != h.end() && it->first < hkey(station + 1, 0); ++it)
        v += it->second * item((int)(it->first % HANGAR_KEY)).volume;
    return v;
}

void World::addItem(Id corp, Id station, int it, int64_t qty) {
    if (qty <= 0) return;
    if (isShipItem(it)) {
        for (int64_t k = 0; k < qty; k++) spawnShip(corp, it - SHIP_FIRST, station);
        return;
    }
    corps[corp].hangar[hkey(station, it)] += qty;
}

bool World::takeItem(Id corp, Id station, int it, int64_t qty) {
    if (qty <= 0) return true;
    if (isShipItem(it)) {
        if (have(corp, station, it) < qty) return false;
        int def = it - SHIP_FIRST;
        int64_t left = qty;
        // hand over bare hulls first, fitted ones last
        for (int pass = 0; pass < 2 && left; pass++)
            for (Id sid : corps[corp].shipList) {
                if (!left) break;
                Ship& s = ships[sid];
                if (!(s.alive && s.corp == corp && s.fleet == NONE && s.station == station && s.def == def)) continue;
                bool fitted = false;
                for (auto m : s.fit) fitted = fitted || m >= 0;
                if (fitted && pass == 0) continue;
                for (auto& m : s.fit)
                    if (m >= 0) { addItem(corp, station, MODULE_FIRST + m, 1); m = -1; }
                s.alive = false;
                if (s.pilot != NONE) { people[s.pilot].ship = NONE; s.pilot = NONE; }
                left--;
            }
        return true;
    }
    auto& h = corps[corp].hangar;
    auto itr = h.find(hkey(station, it));
    if (itr == h.end() || itr->second < qty) return false;
    itr->second -= qty;
    if (itr->second == 0) h.erase(itr);
    return true;
}

Id World::spawnShip(Id corp, int def, Id station) {
    Ship s;
    s.id = (Id)ships.size();
    s.def = def;
    s.corp = corp;
    s.station = station;
    s.hull = shipDef(def).hull;
    s.built = now;
    if (corps[corp].player) {
        int n = 0;
        for (Id x : corps[corp].shipList) n += ships[x].def == def;
        s.name = shipDef(def).name + " " + std::to_string(n + 1);
    }
    ships.push_back(s);
    corps[corp].shipList.push_back(s.id);
    return s.id;
}

void World::pay(Id corp, Money amount, Cat cat, const std::string& memo) {
    if (corp == NONE || amount == 0) return;
    Corp& c = corps[corp];
    c.wallet += amount;
    c.catToday[(int)cat] += amount;
    if (c.player || (std::llabs(amount) >= 1000000 * CR)) {
        c.ledger.push_back({now, cat, amount, memo});
        size_t cap = c.player ? 4000 : 60;
        while (c.ledger.size() > cap) c.ledger.pop_front();
    }
}

void World::addLog(Chan c, Sev s, bool mine, Id system, std::string text) {
    log.push_back({now, c, s, mine, system, std::move(text)});
    while (log.size() > 3000) log.pop_front();
}

ShipStats World::stats(const Ship& s) const {
    const ShipDef& d = shipDef(s.def);
    ShipStats st;
    st.cargo = d.cargo;
    st.oreHold = d.oreHold;
    st.mining = d.miningPerHour;
    st.attack = d.attack;
    st.hull = d.hull;
    st.minutesPerHex = d.minutesPerHex;
    st.evasion = 0;
    st.scan = d.scan;
    for (int k = 0; k < d.slots && k < MAX_SLOTS; k++) {
        int m = s.fit[k];
        if (m < 0) continue;
        const ModuleDef& md = moduleDef(m);
        st.cargo *= md.cargoMul;
        st.oreHold *= md.cargoMul;
        st.mining *= md.miningMul;
        st.attack *= md.attackMul;
        st.hull *= md.hullMul;
        st.minutesPerHex *= md.travelMul;
        st.evasion += md.evasion;
        st.scan += md.scan;
    }
    st.evasion = std::min(0.6, st.evasion);
    return st;
}

Money World::shipValue(const Ship& s) const {
    Money v = item(SHIP_FIRST + s.def).base;
    for (auto m : s.fit)
        if (m >= 0) v += item(MODULE_FIRST + m).base;
    return v;
}

double World::fleetCargoCap(const Fleet& f) const {
    double v = 0;
    for (Id s : f.ships) v += stats(ships[s]).cargo;
    return v;
}
double World::fleetOreCap(const Fleet& f) const {
    double v = 0;
    for (Id s : f.ships) v += stats(ships[s]).oreHold;
    return v;
}
double World::fleetCargoUsed(const Fleet& f, bool oreOnly) const {
    double v = 0;
    for (auto& [it, q] : f.cargo)
        if (!oreOnly || isOre(it)) v += q * item(it).volume;
    return v;
}
double World::fleetFreeFor(const Fleet& f, int it) const {
    double ore = 0, other = 0;
    for (auto& [i, q] : f.cargo) (isOre(i) ? ore : other) += q * item(i).volume;
    double cargo = fleetCargoCap(f), oreHold = fleetOreCap(f);
    double oreOverflow = std::max(0.0, ore - oreHold);
    double freeCargo = std::max(0.0, cargo - other - oreOverflow);
    if (isOre(it)) return std::max(0.0, oreHold - std::min(ore, oreHold)) + freeCargo;
    return freeCargo;
}
Money World::fleetCargoValue(const Fleet& f) const {
    Money v = 0;
    for (auto& [it, q] : f.cargo) v += item(it).base * q;
    return v;
}
double World::pilotFactor(const Ship& s, int skill) const {
    double sk;
    if (corps[s.corp].player) {
        if (s.pilot == NONE) return 0.8;
        const Person& p = people[s.pilot];
        sk = skill == 0 ? p.mining : skill == 1 ? p.nav : p.gunnery;
        if (p.trait == Trait::Reckless && skill == 2) sk += 8;
        if (p.trait == Trait::Steady && skill == 1) sk += 8;
    } else {
        sk = corps[s.corp].aiSkill;
    }
    return 0.8 + 0.4 * std::clamp(sk, 0.0, 100.0) / 100.0;
}
double World::fleetStrength(const Fleet& f) const {
    double v = 0;
    for (Id s : f.ships) {
        const Ship& sh = ships[s];
        ShipStats st = stats(sh);
        v += st.attack * pilotFactor(sh, 2) * (0.5 + 0.5 * sh.hull / st.hull);
    }
    if (corps[f.corp].player) v *= 1.0 + 0.1 * execBonus(f.corp, SEAT_SECURITY);
    return v;
}
double World::fleetEvasion(const Fleet& f) const {
    if (f.ships.empty()) return 0;
    double e = 1.0;
    for (Id s : f.ships) e = std::min(e, stats(ships[s]).evasion);  // the fleet is as hard to catch as its clumsiest ship
    return e;
}
double World::fleetMinutesPerHex(const Fleet& f) const {
    double m = 0;
    for (Id s : f.ships) {
        const Ship& sh = ships[s];
        double x = (pilotFactor(sh, 1) - 0.8) / 0.4;  // 0..1 navigation
        m = std::max(m, stats(sh).minutesPerHex * (1.1 - 0.2 * x));
    }
    if (corps[f.corp].player) m *= 1.0 - 0.1 * execBonus(f.corp, SEAT_LOGISTICS);
    return m;
}
int World::laneMinutes(const Fleet& f, Id from, Id to) const {
    return std::max(3, (int)std::round(fleetMinutesPerHex(f) * laneHexes(from, to) + 1.0));
}
double World::execBonus(Id corp, int seat) const {
    Id p = corps[corp].exec[seat];
    if (p == NONE) return 0;
    const Person& pp = people[p];
    double v = pp.mgmt / 100.0;
    if (pp.seat == seat) v = std::min(1.0, v * 1.15);
    return v;
}
double World::refineYield(Id corp, Id station) const {
    const Station& st = stations[station];
    Zone z = sysOf(station).zone;
    double y = st.isOutpost() ? 0.70 + 0.04 * smodBonus(station, SMOD_REFINERY) : z == Zone::Charter ? 0.55 : 0.62;
    y += 0.10 * execBonus(corp, SEAT_INDUSTRY);
    y += 0.05 * std::clamp(corps[corp].charterStanding / 10.0, 0.0, 1.0);
    return std::min(0.85, y);
}
double World::brokerFee(Id corp) const {
    return 0.025 * (1.0 - 0.3 * execBonus(corp, SEAT_CFO)) * (1.0 - 0.15 * execBonus(corp, SEAT_TRADE)) *
           (1.0 - 0.02 * std::clamp((double)corps[corp].charterStanding, 0.0, 10.0));
}
double World::salesTax(Id corp, Id station) const {
    double cfo = (1.0 - 0.3 * execBonus(corp, SEAT_CFO)) * (1.0 - 0.15 * execBonus(corp, SEAT_TRADE));
    if (station == NONE) return 0.03 * cfo;
    const Station& st = stations[station];
    if (st.owner != NONE) return st.owner == corp ? 0.0 : st.marketTax;  // outposts and colonies: the owner's rate
    return (systems[st.system].zone == Zone::Charter ? 0.025 + charterLevy : 0.025) * cfo;
}

bool World::hostileTo(Id a, Id b) const {
    if (a == b || a == NONE || b == NONE) return false;
    uint8_t p = pactBits(a, b);
    if (p & 1) return true;        // at war
    if (p & 6) return false;       // allies and pact partners hold fire
    if (standing(a, b) <= -5.0f) return true;
    if (isOutlaw(b) && (corps[a].arch == Arch::Mercenary || corps[a].player)) return true;
    return false;
}

std::string World::fleetLocation(const Fleet& f) const {
    if (f.docked != NONE) return stations[f.docked].name;
    const StarSystem& s = systems[f.system];
    if (f.act == Act::Jump && f.jumpTo != NONE) return s.name + " > " + systems[f.jumpTo].name;
    if (f.atBelt) return s.name + " belt";
    return s.name;
}

Tick World::fleetEta(const Fleet& f) const {
    Tick t = std::max<Tick>(0, f.actEnd - now);
    Id at = f.act == Act::Jump && f.jumpTo != NONE ? f.jumpTo : f.system;
    double mph = fleetMinutesPerHex(f);
    for (Id nx : f.path) {
        if (nx == at) continue;
        t += (Tick)std::round(mph * laneHexes(at, nx) + 1.0);
        at = nx;
    }
    return t;
}

void World::markActive(Id system) {
    if (system == NONE) return;
    for (Id sid : systems[system].stations) {
        Station& st = stations[sid];
        if (st.dead) continue;
        st.activeUntil = std::max(st.activeUntil, now + 6 * MINUTES_PER_HOUR);
        if (!st.makerLive) {
            double hours = (double)(now - st.lastEconomy) / 60.0;
            if (hours > 0) stationEconomy(st, hours);
            st.lastEconomy = now;
            makerRepost(st);
        }
    }
}

// ------------------------------------------------------------ scheduling ----
void World::tick() {
    now++;
    for (auto& f : fleets)
        if (f.alive) fleetTick(f);
    for (auto& b : battles)
        if (b.active) battleTick(b);
    jobsTick();
    if (now % 10 == 0) {
        rebuildFleetIndex();
        updateSight();
        divisionsTick();
        for (auto& f : fleets)
            if (f.alive && f.docked == NONE && f.battle == NONE &&
                (f.order.kind == OrderKind::Raid || f.order.kind == OrderKind::Patrol || f.order.kind == OrderKind::Siege))
                scanForTargets(f);
    }
    int minute = (int)(now % 60);
    for (size_t sid = (size_t)minute; sid < stations.size(); sid += 60) {
        Station& st = stations[sid];
        if (st.dead) continue;
        bool active = st.type == StationType::TradeHub || st.activeUntil > now;
        if (!active && st.makerLive) {
            for (auto& bk : st.books) {
                auto isMaker = [](const Order& o) { return o.corp == NONE; };
                bk.sells.erase(std::remove_if(bk.sells.begin(), bk.sells.end(), isMaker), bk.sells.end());
                bk.buys.erase(std::remove_if(bk.buys.begin(), bk.buys.end(), isMaker), bk.buys.end());
            }
            st.makerLive = false;
        }
        Tick since = now - st.lastEconomy;
        if (active ? since >= 60 : since >= 360) {
            stationEconomy(st, since / 60.0);
            st.lastEconomy = now;
            makerSweep(st);
            if (active) makerRepost(st);
        }
    }
    for (auto& c : corps)
        if (!c.player && c.alive && now >= c.nextThink) {
            c.nextThink = now + 60 + rng.irange(0, 20);
            aiThink(c);
        }
    if (minute == 0) hourly();
    if (now % MINUTES_PER_DAY == 0) daily();
    if (now % (7 * MINUTES_PER_DAY) == 0) weekly();
}

void World::hourly() {
    expireOrders();
    contractsHourly();
    spyHourly();
    projectsHourly();
    outpostsHourly();
    eventsHourly();
    diplomacyHourly();
    // the player's sensors keep its picture of nearby markets fresh
    if (fog)
        for (size_t sy = 0; sy < sight.size(); sy++)
            if (sight[sy] >= 2)
                for (Id st : systems[sy].stations) snapshotStation(st);
    // hostile fleet-hours feed the risk rating
    for (auto& f : fleets)
        if (f.alive && f.docked == NONE && (f.order.kind == OrderKind::Raid || f.order.kind == OrderKind::Siege || isOutlaw(f.corp)))
            systems[f.system].hostileHours += 1.0f;
    Corp& p = pc();
    p.netWorth = computeNetWorth(player);
    pstats.bestNetWorth = std::max(pstats.bestNetWorth, p.netWorth);
    if (now % (MINUTES_PER_DAY * 3) == 0) {
        // compact finished battles
        battles.erase(std::remove_if(battles.begin(), battles.end(), [](const Battle& b) { return !b.active; }), battles.end());
        for (size_t i = 0; i < battles.size(); i++) {
            battles[i].id = (Id)i;
            for (Id f : battles[i].att) fleets[f].battle = (Id)i;
            for (Id f : battles[i].def) fleets[f].battle = (Id)i;
        }
    }
}

void World::daily() {
    int day = (int)(now / MINUTES_PER_DAY);
    // close the trading day; quiet books away from the hubs drop their history after a month
    for (auto& st : stations) {
        if (st.dead) continue;
        bool hub = st.type == StationType::TradeHub;
        size_t cap = hub ? 90 : 45;
        for (auto& bk : st.books) {
            int i = bk.item;
            bool keep = hub ? st.target[i] > 0 || bk.tradedToday : bk.tradedToday || bk.lastCorpTradeDay >= day - 30;
            if (!keep) {
                if (!bk.hist.empty()) { bk.hist.clear(); bk.hist.shrink_to_fit(); }
                bk.tradedToday = false;
                continue;
            }
            Bar b = bk.today;
            if (!bk.tradedToday) {
                float p = (float)toCr(bk.last ? bk.last : refPrice(st.id, i));
                b = Bar{day - 1, p, p, p, p, 0};
            }
            b.day = day - 1;
            bk.hist.push_back(b);
            if (bk.hist.size() > cap) bk.hist.erase(bk.hist.begin());
            bk.tradedToday = false;
            bk.today = Bar{};
        }
    }
    for (auto& s : systems) {
        for (auto& b : s.belts) b.remaining = std::min(b.capacity, b.remaining + b.capacity * 0.02);
    }
    financeDaily();
    peopleDaily();
    divisionsDaily();
    projectsDaily();
    coloniesDaily();
    capitalDaily();
    eventsDaily();
    riskDaily();
    contractsDaily();
    outpostsDaily();
    exploreDaily();
    diplomacyDaily();
    intelDaily();
    for (auto& c : corps) {
        if (!c.alive) continue;
        c.netWorth = computeNetWorth(c.id);
        c.catDays.push_back(c.catToday);
        c.catToday.fill(0);
        while (c.catDays.size() > 60) c.catDays.pop_front();
        c.stats.push_back({day, c.wallet, c.netWorth});
        if (c.stats.size() > 400) c.stats.erase(c.stats.begin());
        // prune dead entries from the derived lists
        c.shipList.erase(std::remove_if(c.shipList.begin(), c.shipList.end(),
                                        [&](Id s) { return !ships[s].alive || ships[s].corp != c.id; }),
                         c.shipList.end());
        c.fleetList.erase(std::remove_if(c.fleetList.begin(), c.fleetList.end(),
                                         [&](Id f) { return !fleets[f].alive || fleets[f].corp != c.id; }),
                          c.fleetList.end());
    }
    sharesDaily();
    corporateDaily();
}

void World::weekly() { riskWeekly(); }

// ----------------------------------------------------------------- saves ----
bool World::save(const std::string& path, std::string* err) const {
    try {
        Writer w;
        uint32_t magic = SAVE_MAGIC, ver = SAVE_VERSION;
        w(magic, ver);
        const_cast<World*>(this)->ser(w);
        std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { if (err) *err = "cannot write " + tmp; return false; }
            f.write((const char*)w.buf.data(), (std::streamsize)w.buf.size());
            if (!f) { if (err) *err = "write failed"; return false; }
        }
        std::remove(path.c_str());
        if (std::rename(tmp.c_str(), path.c_str()) != 0) { if (err) *err = "cannot replace " + path; return false; }
        return true;
    } catch (std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool World::load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    try {
        Reader r(buf);
        uint32_t magic = 0, ver = 0;
        r(magic, ver);
        if (magic != SAVE_MAGIC) { if (err) *err = "not a NebulaOne save"; return false; }
        if (ver != SAVE_VERSION) {
            if (err) *err = ver < SAVE_VERSION ? "this save is from an older NebulaOne (0.1 or 0.2) and the universe has changed too much to load it"
                                               : "save version " + std::to_string(ver) + " is newer than this build";
            return false;
        }
        World w;
        w.ser(r);
        *this = std::move(w);
        rebuildDerived();
        return true;
    } catch (std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

}  // namespace nb
