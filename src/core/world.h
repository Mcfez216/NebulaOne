// NebulaOne - the World: every piece of simulation state, and the rules that
// change it. Nothing in src/core includes a UI header; the game and (later) a
// server both drive the World only through World::execute(Command).
#pragma once
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "items.h"
#include "rng.h"
#include "types.h"

namespace nb {

// ------------------------------------------------------------------ universe --
enum class StationType : uint8_t { TradeHub = 0, Industrial, Agricultural, MiningColony, Research, Military, Outpost, Colony };
constexpr int STATION_TYPE_COUNT = 8;

// ------------------------------------------------------------------ planets --
// Every system's planets are worked out from the seed when asked for, so a
// 20,000-system chart stores none of them; only colonies are saved.
enum class PlanetType : uint8_t { Garden = 0, Ocean, Rocky, Ice, Volcanic, Barren, GasGiant, COUNT };
constexpr int PLANET_TYPE_COUNT = (int)PlanetType::COUNT;
const char* planetTypeName(PlanetType t);
struct Planet {
    int index = 0;          // 0 = I
    PlanetType type = PlanetType::Barren;
    int size = 1;           // 1 small .. 5 huge
    float hab = 0;          // habitability 0..1
    float richness = 1;     // 0.5 .. 2
    std::string name;
};
// What a colony on this kind of world produces, as (item, share of the value).
const std::vector<std::pair<int, float>>& planetOutputs(PlanetType t);
const char* stationTypeName(StationType t);

enum class GalaxySize : uint8_t { Compact = 0, Standard, Vast, Galaxy, Expanse };
constexpr int GALAXY_SIZE_COUNT = 5;
const char* galaxySizeName(GalaxySize g);
int galaxySystemTarget(GalaxySize g);

// Hex coordinates are axial (q, r) on a flat-topped grid.
struct Hex {
    int q = 0, r = 0;
    bool operator==(const Hex& o) const { return q == o.q && r == o.r; }
};
inline int hexDistance(Hex a, Hex b) {
    int dq = a.q - b.q, dr = a.r - b.r;
    int ds = -dq - dr;
    return (std::abs(dq) + std::abs(dr) + std::abs(ds)) / 2;
}

struct HexCell {
    int16_t q = 0, r = 0;
    Id region = NONE;
    Zone zone = Zone::Frontier;
    Id system = NONE;
    template <class A> void ser(A& a) { a(q, r, region, zone, system); }
};

struct Region {
    std::string name;
    float cx = 0, cy = 0;  // map units (centre of its systems)
    Zone zone = Zone::Contract;  // dominant zone, for labels
    template <class A> void ser(A& a) { a(name, cx, cy, zone); }
};

struct Belt {
    int ore = 0;
    double remaining = 0, capacity = 0;
    Id knownBy = NONE;  // a hidden pocket found by survey: only this corp knows it is there (NONE = charted)
    Tick until = 0;     // pockets and gold-rush belts play out; 0 = permanent
    template <class A> void ser(A& a) { a(ore, remaining, capacity, knownBy, until); }
};

struct LaneLink {
    Id to = NONE;
    int16_t hexes = 1;  // lane length in hexes: sets travel time
    template <class A> void ser(A& a) { a(to, hexes); }
};

struct StarSystem {
    Id id = NONE;
    std::string name;
    std::string hexId;  // wargame-style "CCRR" designation
    Id region = NONE;
    Hex hex;
    float x = 0, y = 0;  // map units derived from the hex
    Zone zone = Zone::Charter;
    int depth = 0;       // 0 core .. 3 deep Frontier: sets which ores appear
    float risk = 10;     // underwriters' published rating (0..100), re-rated weekly
    float riskLive = 10; // what the rating would be today (drives the outlook)
    float losses = 0;    // value destroyed here, in millions of credits, decaying
    float heat = 0;      // recent violence, decays daily
    float hostileHours = 0;  // hostile fleet-hours this week
    Id owner = NONE;     // sovereignty: corp holding an outpost here
    uint8_t beacon = 0;  // Charter Beacon: 2 here, 1 next door: the Wardens answer violence in Contract space
    Id surveyArray = NONE;  // a Deep Survey Array's owner
    std::vector<LaneLink> lanes;
    std::vector<Belt> belts;
    std::vector<Id> stations;
    template <class A> void ser(A& a) {
        a(id, name, hexId, region, hex.q, hex.r, x, y, zone, depth, risk, riskLive, losses, heat, hostileHours, owner, lanes, belts,
          stations, beacon, surveyArray);
    }
};

// ------------------------------------------------------------------- markets --
struct Order {
    int64_t id = 0;
    Id corp = NONE;  // NONE = the station's own population (market maker)
    int item = 0;
    bool buy = false;
    Money price = 0;
    int64_t qty = 0, qty0 = 0;
    Tick placed = 0, expires = 0;
    template <class A> void ser(A& a) { a(id, corp, item, buy, price, qty, qty0, placed, expires); }
};

struct Bar {  // one trading day, compact (prices in credits)
    int32_t day = 0;
    float open = 0, high = 0, low = 0, close = 0, volume = 0;
    template <class A> void ser(A& a) { a(day, open, high, low, close, volume); }
};

struct Book {
    int16_t item = -1;
    std::vector<Order> sells;  // ascending price
    std::vector<Order> buys;   // descending price
    std::vector<Bar> hist;     // recent days, only where trading happens
    Bar today{};
    bool tradedToday = false;
    int32_t lastCorpTradeDay = -1000;  // last day a corporation traded here
    Money last = 0;
    template <class A> void ser(A& a) { a(item, sells, buys, hist, today, tradedToday, lastCorpTradeDay, last); }
};

enum class OutpostState : uint8_t { None = 0, Anchoring, Online, Offline, Reinforced };
const char* outpostStateName(OutpostState s);

// The modules an outpost can be upgraded with, each to tier 3.
enum SMod : int { SMOD_REFINERY = 0, SMOD_FACTORY, SMOD_MARKET, SMOD_LAB, SMOD_DEFENCE, SMOD_SILO, SMOD_COUNT };
const char* smodName(int m);
const char* smodEffect(int m);  // what one tier does
constexpr int SMOD_MAX_TIER = 3;

struct Station {
    Id id = NONE;
    Id system = NONE;
    std::string name;
    StationType type = StationType::TradeHub;
    bool dead = false;
    // population economy, per item (float keeps big galaxies compact)
    std::vector<float> stock, target, prod, cons, eventMul;
    float prodMul = 1;            // galaxy events: a strike stops production
    std::vector<int16_t> bookOf;  // per item: index into books, -1 = never traded here
    std::deque<Book> books;       // only items that have a market here (deque: references stay valid)
    bool makerLive = false;       // population quotes are posted as real orders (stations near activity)
    Tick lastEconomy = 0;
    Tick activeUntil = 0;         // stations near activity update hourly, others every few hours
    // outposts
    Id owner = NONE;
    OutpostState state = OutpostState::None;
    Tick stateUntil = 0;
    float marketTax = 0.05f, refineTax = 0.05f;
    double shield = 0, hull = 0;
    Id besieger = NONE;
    Tick lastSiege = 0;
    Money taxCollected = 0;
    std::array<uint8_t, SMOD_COUNT> smod{};  // module tiers (outposts)
    bool nexus = false;                      // a Trade Nexus: the outpost trades and hires like a hub
    template <class A> void ser(A& a) {
        a(id, system, name, type, dead, stock, target, prod, cons, eventMul, prodMul, bookOf, books, makerLive, lastEconomy, activeUntil,
          owner, state, stateUntil, marketTax, refineTax, shield, hull, besieger, lastSiege, taxCollected, smod, nexus);
    }
    Book* findBook(int i) { int k = bookOf[i]; return k < 0 ? nullptr : &books[k]; }
    const Book* findBook(int i) const { int k = bookOf[i]; return k < 0 ? nullptr : &books[k]; }
    Book& book(int i) {
        if (bookOf[i] < 0) {
            bookOf[i] = (int16_t)books.size();
            books.emplace_back();
            books.back().item = (int16_t)i;
        }
        return books[bookOf[i]];
    }
    bool isOutpost() const { return type == StationType::Outpost; }
    bool isColony() const { return type == StationType::Colony; }
};

// ---------------------------------------------------------------- corporations --
enum class Arch : uint8_t { Player = 0, Trader, Miner, Industrialist, Pirate, Ganker, Mercenary };
const char* archName(Arch a);

enum class Cat : uint8_t {
    MarketSale = 0, MarketPurchase, BrokerFee, SalesTax, RefineFee, ManufactureFee, Wages, Hiring,
    LoanIn, LoanRepay, LoanInterest, LendOut, LendRepaid, Dividends, Insurance, InsurancePayout,
    Repairs, ShareTrade, Bounty, Upkeep, Contracts, Taxes, ResearchFee, Equity, Espionage,
    Futures, Margin, Bonds, Coupons, Underwriting, Other, COUNT
};
constexpr int CAT_COUNT = (int)Cat::COUNT;
const char* catLabel(Cat c);
bool catIsCapital(Cat c);

struct LedgerLine {
    Tick t = 0;
    Cat cat = Cat::Other;
    Money amount = 0;
    std::string memo;
    template <class A> void ser(A& a) { a(t, cat, amount, memo); }
};

struct DayStat {
    int32_t day = 0;
    Money wallet = 0, netWorth = 0;
    template <class A> void ser(A& a) { a(day, wallet, netWorth); }
};

enum Seat : int { SEAT_CFO = 0, SEAT_INDUSTRY, SEAT_SECURITY, SEAT_LOGISTICS, SEAT_MINING, SEAT_TRADE, SEAT_INTEL, SEAT_COUNT };
const char* seatName(int s);

// -------------------------------------------------------------- divisions --
// The company split into divisions, each run by the director in its seat. A
// director with automation on runs the division's fleets inside the limits set
// for it, and reports every 30 days.
enum Division : int { DIV_MINING = 0, DIV_TRADE, DIV_HAULAGE, DIV_INDUSTRY, DIV_SECURITY, DIV_INTEL, DIV_COUNT };
const char* divisionName(int d);
int divisionSeat(int d);  // the executive seat whose holder runs it
int seatDivision(int seat);  // -1 for the CFO

struct DivReport {
    int32_t month = 0;
    Money revenue = 0, costs = 0;
    int fleets = 0;
    float score = 0;  // the month's mark, -1 .. +1
    std::string note;
    template <class A> void ser(A& a) { a(month, revenue, costs, fleets, score, note); }
};

struct DivPolicy {
    bool automate = false;   // the director runs the division
    Id base = NONE;          // its home station (NONE = headquarters)
    int radius = 6;          // jumps from base it works within
    uint8_t zones = 1;       // 1 Charter, 2 Contract, 4 Frontier
    int maxRisk = 45;        // the riskiest system it sends ships into (0..100)
    Money budget = 0;        // trade: capital in cargo at once; industry, haulage, intelligence: spending per 30 days
    bool sell = true;        // mining: sell the minerals; industry: sell what it builds
    bool insure = true;      // trade and haulage: insure cargo
    int product = -1;        // industry: the blueprint to build (-1 = the director picks)
    int lines = 2;           // industry: jobs at once
    bool research = true;    // industry: research blueprints when a line is free
    bool fuel = true, consolidate = true, couriers = true;  // haulage
    bool escort = true, guard = true, bounties = false;      // security
    bool projects = true;    // haulage: carry materials from the base to your projects
    bool supply = true;      // haulage: keep your colonies supplied
    template <class A> void ser(A& a) {
        a(automate, base, radius, zones, maxRisk, budget, sell, insure, product, lines, research, fuel, consolidate, couriers, escort,
          guard, bounties, projects, supply);
    }
};

struct DivisionState {
    DivPolicy pol;
    Money revenue = 0, costs = 0, spent = 0;  // this month so far; spent counts against the budget
    std::vector<DivReport> reports;          // the last 12 months
    Tick nextThink = 0;
    std::string status;
    std::vector<int64_t> leftovers;  // trade: cargo that did not sell on arrival (hangar keys), sold off later
    template <class A> void ser(A& a) { a(pol, revenue, costs, spent, reports, nextThink, status, leftovers); }
};

struct Corp {
    Id id = NONE;
    std::string name, ticker;
    Arch arch = Arch::Player;
    bool player = false;
    bool alive = true;
    Money wallet = 0;
    Id home = NONE;
    std::map<int64_t, int64_t> hangar;  // key = station * HANGAR_KEY + item (ships are separate entities)
    std::deque<LedgerLine> ledger;
    std::array<Money, CAT_COUNT> catToday{};
    std::deque<std::array<Money, CAT_COUNT>> catDays;
    std::vector<DayStat> stats;
    std::array<Id, SEAT_COUNT> exec{NONE, NONE, NONE, NONE, NONE, NONE, NONE};
    std::array<DivisionState, DIV_COUNT> divs;
    float smear = 0;             // a smear campaign's drag on the share price (0..0.4), fading
    bool poisonPill = false;     // takeover defence: a raider past 20% is diluted
    Id raider = NONE;            // the player: a rival building a stake; it wants paying off (greenmail)
    Tick raidDeadline = 0;
    uint8_t defenceUsed = 0;     // 1 = a poison pill has fired, 2 = a white knight has stepped in
    std::map<Id, int64_t> shares;  // corp id -> shares held
    int64_t sharesOut = 1000000;
    Money sharePrice = 0;
    std::vector<Money> shareHist;
    Money netWorth = 0;
    Money profitPeriod = 0;  // net worth at the last dividend date
    int rating = 2;          // 0 = A ... 4 = E
    float aiSkill = 50;
    float charterStanding = 0;  // with the Charter Authority; <= -5 is outlaw
    std::map<Id, float> standing;  // sparse: only corps you have dealt with
    int brokeDays = 0;
    Tick nextThink = 0;
    Tick cooldownUntil = 0;
    int shipsLost = 0, shipsKilled = 0;
    std::array<uint8_t, BLUEPRINT_COUNT> me{}, te{};  // research levels known to the corporation
    // the stock market, for the player after an IPO
    bool listed = false;
    int64_t publicFloat = 0;     // shares held by outside investors
    float board = 60;            // board confidence 0..100
    float boardTarget = 0.08f;   // growth expected per 30 days
    Money reviewBase = 0;
    Tick nextReview = 0;
    Tick founded = 0;
    Id parent = NONE;            // merged into this corp
    uint8_t livery = 0;          // interface and fleet colour (cosmetic)
    Id lastAggressor = NONE;     // the last corporation to besiege its outposts or attack it
    Tick lastAggression = -100000;
    // derived, rebuilt after loading (not saved)
    std::vector<Id> shipList, fleetList;
    std::set<int64_t> orderSpots;  // hangar keys of books holding this corp's orders
    template <class A> void ser(A& a) {
        a(id, name, ticker, arch, player, alive, wallet, home, hangar, ledger, catToday, catDays, stats, exec, shares, sharesOut,
          sharePrice, shareHist, netWorth, profitPeriod, rating, aiSkill, charterStanding, standing, brokeDays, nextThink,
          cooldownUntil, shipsLost, shipsKilled, me, te, listed, publicFloat, board, boardTarget, reviewBase, nextReview, founded,
          parent, livery, lastAggressor, lastAggression, divs, smear, poisonPill, raider, raidDeadline,
          defenceUsed);
    }
};

// -------------------------------------------------------------------- people --
enum class Role : uint8_t { Pilot = 0, Executive };
enum class Trait : uint8_t { None = 0, Steady, Reckless, Frugal, Ambitious };
const char* traitName(Trait t);

struct Person {
    Id id = NONE;
    std::string name;
    Role role = Role::Pilot;
    int seat = -1;
    int mining = 50, nav = 50, gunnery = 50, mgmt = 50;
    Trait trait = Trait::None;
    Money wage = 0;
    Id employer = NONE;
    Id ship = NONE;
    Id hub = NONE;
    Tick availableUntil = 0;
    Tick injuredUntil = 0;
    bool alive = true;
    float loyalty = 70;   // 0..100: below 20 they hand in their notice; low loyalty invites poachers
    int ambition = 50;    // 0..100: ambitious directors ask for raises after a good month
    Tick hired = 0;
    template <class A> void ser(A& a) {
        a(id, name, role, seat, mining, nav, gunnery, mgmt, trait, wage, employer, ship, hub, availableUntil, injuredUntil, alive, loyalty,
          ambition, hired);
    }
};

// --------------------------------------------------------------------- ships --
constexpr int MAX_SLOTS = 4;
struct Ship {
    Id id = NONE;
    int def = 0;
    Id corp = NONE;
    Id fleet = NONE;
    Id station = NONE;
    Id pilot = NONE;
    double hull = 0;
    bool alive = true;
    bool insured = false;
    Tick insuredUntil = 0;
    std::string name;
    std::array<int8_t, MAX_SLOTS> fit{-1, -1, -1, -1};  // module index per slot
    Tick built = 0;
    int kills = 0, trips = 0;
    double mined = 0, hauled = 0;
    template <class A> void ser(A& a) {
        a(id, def, corp, fleet, station, pilot, hull, alive, insured, insuredUntil, name, fit, built, kills, trips, mined, hauled);
    }
};

enum class OrderKind : uint8_t { None = 0, Move, Mine, Haul, Trade, Patrol, Raid, Siege, Survey, Salvage, Scout };
const char* orderKindName(OrderKind k);
enum class RoutePolicy : uint8_t { Shortest = 0, LowestRisk, AvoidFrontier };
const char* policyName(RoutePolicy p);

struct FleetOrder {
    OrderKind kind = OrderKind::None;
    Id station = NONE;
    Id station2 = NONE;
    Id system = NONE;
    int belt = -1;
    int item = -1;
    int64_t qty = 0;
    Money price = 0;
    Money price2 = 0;
    bool loop = false;
    Id targetCorp = NONE;
    RoutePolicy policy = RoutePolicy::Shortest;
    bool insureCargo = false;
    Id contract = NONE;  // courier / supply contract this run serves
    Id site = NONE;      // salvage target
    Id project = NONE;   // haul: deliver the cargo to this project on arrival
    Id colony = NONE;    // haul: hand the cargo to this colony on arrival
    template <class A> void ser(A& a) {
        a(kind, station, station2, system, belt, item, qty, price, price2, loop, targetCorp, policy, insureCargo, contract, site, project,
          colony);
    }
};

enum class Act : uint8_t { Idle = 0, Undock, Dock, Jump, ToBelt, Mine, Load, Unload, Wait, Combat, Survey, Salvage };
const char* actName(Act a);

struct Fleet {
    Id id = NONE;
    Id corp = NONE;
    std::string name;
    std::vector<Id> ships;
    Id system = NONE;
    Id docked = NONE;
    bool atBelt = false;
    Act act = Act::Idle;
    Tick actStart = 0, actEnd = 0;
    Id jumpTo = NONE;
    std::vector<Id> path;
    FleetOrder order;
    int phase = 0;
    std::map<int, int64_t> cargo;
    Id battle = NONE;
    std::string status;
    bool alive = true;
    Tick nextScan = 0;
    Money spent = 0, earned = 0;
    int trips = 0;
    Money cargoCover = 0;  // insured cargo value for the trip under way
    int8_t division = -1;  // run by this division's director (-1 = by you)
    template <class A> void ser(A& a) {
        a(id, corp, name, ships, system, docked, atBelt, act, actStart, actEnd, jumpTo, path, order, phase, cargo, battle, status, alive,
          nextScan, spent, earned, trips, cargoCover, division);
    }
};

struct Battle {
    Id id = NONE;
    Id system = NONE;
    std::vector<Id> att, def;
    Id attCorp = NONE, defCorp = NONE;
    int round = 0;
    int policeRound = -1;
    bool lawful = false;
    double attStart = 0;
    bool active = true;
    std::map<int, int64_t> loot;
    int attLost = 0, defLost = 0;
    Money valueLost = 0;
    template <class A> void ser(A& a) {
        a(id, system, att, def, attCorp, defCorp, round, policeRound, lawful, attStart, active, loot, attLost, defLost, valueLost);
    }
};

// ------------------------------------------------------------------ industry --
enum class JobKind : uint8_t { Refine = 0, Manufacture, ResearchME, ResearchTE, Copy };
const char* jobKindName(JobKind k);

struct Job {
    Id id = NONE;
    Id corp = NONE;
    Id station = NONE;
    JobKind kind = JobKind::Refine;
    int item = 0;       // ore (refine) or blueprint index
    int64_t runs = 0;
    Tick start = 0, end = 0;
    std::vector<std::pair<int, int64_t>> out;
    bool active = true;
    bool usedCopies = false;
    int8_t division = -1;  // started by a division's director
    template <class A> void ser(A& a) { a(id, corp, station, kind, item, runs, start, end, out, active, usedCopies, division); }
};

// ------------------------------------------------------------------ contracts --
enum class ContractKind : uint8_t { Supply = 0, Courier, Bounty, Patrol };
enum class ContractState : uint8_t { Open = 0, Accepted, Done, Failed, Expired, Cancelled };
const char* contractKindName(ContractKind k);
const char* contractStateName(ContractState s);

struct Contract {
    Id id = NONE;
    ContractKind kind = ContractKind::Supply;
    ContractState state = ContractState::Open;
    Id issuer = NONE;         // corp, or NONE for a station / the Charter Authority
    std::string issuerName;
    Id contractor = NONE;
    Id from = NONE, to = NONE;  // stations
    Id system = NONE;           // patrol sector
    Id target = NONE;           // bounty target corp
    int item = -1;
    int64_t qty = 0;
    Money reward = 0, collateral = 0;
    Tick posted = 0, expires = 0, deadline = 0;
    int64_t progress = 0;  // kills or patrol hours
    int64_t goal = 0;
    template <class A> void ser(A& a) {
        a(id, kind, state, issuer, issuerName, contractor, from, to, system, target, item, qty, reward, collateral, posted, expires,
          deadline, progress, goal);
    }
};

// ------------------------------------------------------------------- finance --
struct Loan {
    Id id = NONE;
    Id lender = NONE;
    Id borrower = NONE;
    Money principal = 0, remaining = 0;
    double rate30 = 0.02;
    int days = 30;
    Money daily = 0;
    Tick start = 0;
    int missed = 0;
    int status = 0;
    Money recovered = 0;
    template <class A> void ser(A& a) { a(id, lender, borrower, principal, remaining, rate30, days, daily, start, missed, status, recovered); }
};

struct LoanRequest {
    Id id = NONE;
    Id corp = NONE;
    Money amount = 0;
    double rate30 = 0.03;
    int days = 30;
    int rating = 2;
    Tick expires = 0;
    bool open = true;
    template <class A> void ser(A& a) { a(id, corp, amount, rate30, days, rating, expires, open); }
};

// --------------------------------------------------------------- galaxy events --
// Things that happen to the galaxy and move its markets: some are announced
// ahead of time, so the wire is worth reading.
enum class EventKind : uint8_t { Shortage = 0, Glut, Strike, Boom, Plague, Levy, Embargo, Storm, PirateSurge, GoldRush, Crackdown, COUNT };
constexpr int EVENT_KIND_COUNT = (int)EventKind::COUNT;
const char* eventKindName(EventKind k);

struct GalaxyEvent {
    Id id = NONE;
    EventKind kind = EventKind::Shortage;
    Id region = NONE, system = NONE, station = NONE;
    int item = -1;
    float mag = 1;
    Tick posted = 0, start = 0, end = 0;
    bool applied = false, over = false;
    int belt = -1;  // gold rush: the belt it opened
    std::string headline;
    template <class A> void ser(A& a) { a(id, kind, region, system, station, item, mag, posted, start, end, applied, over, belt, headline); }
};

// ----------------------------------------------------------------- exploration --
enum class SiteKind : uint8_t { Derelict = 0, Relic, Pocket, Drift };
const char* siteKindName(SiteKind k);

struct Site {
    Id id = NONE;
    Id system = NONE;
    SiteKind kind = SiteKind::Derelict;
    uint64_t key = 0;     // (system, fortnight, slot): each hidden site can be found once
    int level = 0;        // 0 core .. 3 deep Frontier: how rich it is
    Id finder = NONE;
    Tick found = 0, expires = 0;
    bool done = false;    // salvaged, mined out or closed
    Id link = NONE;       // drift lane: the far end
    int hexes = 0;        // drift lane: length
    int belt = -1;        // pocket: the belt it added
    std::string name;
    template <class A> void ser(A& a) { a(id, system, kind, key, level, finder, found, expires, done, link, hexes, belt, name); }
};

struct DriftEdge {  // derived from open drift sites
    Id a = NONE, b = NONE, corp = NONE;
    int hexes = 0;
};

// -------------------------------------------------------------------- intel --
struct SnapRow {
    int16_t item = 0;
    Money ask = 0, bid = 0;
    int64_t askQty = 0, bidQty = 0;
    template <class A> void ser(A& a) { a(item, ask, bid, askQty, bidQty); }
};
struct MarketSnap {  // what the player last saw of a station's market
    Tick t = -1;
    std::vector<SnapRow> rows;
    template <class A> void ser(A& a) { a(t, rows); }
    const SnapRow* find(int item) const {
        for (auto& r : rows)
            if (r.item == item) return &r;
        return nullptr;
    }
};

// --------------------------------------------------------------- diplomacy --
enum class Pact : uint8_t { War = 0, Alliance, NonAggression };
const char* pactName(Pact p);
struct Treaty {
    Id id = NONE;
    Pact kind = Pact::War;
    Id a = NONE, b = NONE;  // war: a declared on b
    Tick start = 0, until = 0;
    bool active = true;
    Money scoreA = 0, scoreB = 0;  // value each side has destroyed
    Id hiredBy = NONE;             // mercenary contract: who pays
    Id onBehalf = NONE;            // an ally joined this war for this corp
    std::string why;
    template <class A> void ser(A& a_) { a_(id, kind, a, b, start, until, active, scoreA, scoreB, hiredBy, onBehalf, why); }
    bool live(Tick now) const { return active && now >= start && now < until; }
};

enum class OfferKind : uint8_t { Peace = 0, Alliance, NonAggression, JoinWar };
const char* offerKindName(OfferKind k);
struct Offer {
    Id id = NONE;
    OfferKind kind = OfferKind::Peace;
    Id from = NONE, to = NONE;
    Id treaty = NONE;   // peace: the war; join war: the war to join
    Money amount = 0;   // peace: paid by `from` to `to` (negative: `from` demands it)
    Tick expires = 0;
    bool open = true;
    std::string note;
    template <class A> void ser(A& a) { a(id, kind, from, to, treaty, amount, expires, open, note); }
};

// ------------------------------------------------------------ construction --
// Station upgrades, and megaprojects anyone can pay into: jump gates (a new lane
// for everyone, with tolls for the backers), Charter Beacons (the Wardens'
// reach into Contract space), Trade Nexuses (an outpost becomes a hub) and Deep
// Survey Arrays (sensors and hidden sites for the founder).
enum class ProjectKind : uint8_t { Upgrade = 0, JumpGate, Beacon, Nexus, SurveyArray, ColonyUpgrade, COUNT };
constexpr int PROJECT_KIND_COUNT = (int)ProjectKind::COUNT;
const char* projectKindName(ProjectKind k);
enum class ProjectStage : uint8_t { Funding = 0, Building, Done, Cancelled };

struct Project {
    Id id = NONE;
    ProjectKind kind = ProjectKind::Upgrade;
    ProjectStage stage = ProjectStage::Funding;
    Id founder = NONE;
    std::string name;
    Id system = NONE, target = NONE;  // gate: the far end
    Id station = NONE;                // upgrade and nexus: the station
    Id yard = NONE;                   // where materials are delivered
    int module = -1, tier = 0;        // upgrade
    std::vector<std::pair<int, int64_t>> need;
    std::vector<int64_t> got;         // per need line
    Money cash = 0, cashGot = 0;
    std::map<Id, Money> share;        // contributed value per corporation
    Tick founded = 0, buildEnd = 0;
    int buildDays = 0;
    Money tolls = 0;                  // gate: collected so far
    template <class A> void ser(A& a) {
        a(id, kind, stage, founder, name, system, target, station, yard, module, tier, need, got, cash, cashGot, share, founded, buildEnd,
          buildDays, tolls);
    }
    double progress() const;          // 0..1 of the value needed
};

// ------------------------------------------------------------- capital markets --
// Futures on the Charter Exchange (cash-settled against the hub's price),
// corporate bonds, short selling, and an underwriting desk of your own.
struct Future {
    Id id = NONE;
    Id corp = NONE, hub = NONE;
    int item = 0;
    int64_t qty = 0;
    bool isLong = true;
    Money price = 0, margin = 0, result = 0;
    Tick opened = 0, expiry = 0;
    bool open = true;
    template <class A> void ser(A& a) { a(id, corp, hub, item, qty, isLong, price, margin, result, opened, expiry, open); }
};

constexpr Money BOND_UNIT = 1000000 * CR;  // one bond: 1M cr face value
struct Bond {
    Id id = NONE;
    Id issuer = NONE;
    int64_t units = 0, onOffer = 0;  // on offer: not yet sold to anyone
    double rate30 = 0.01;            // coupon per 30 days
    Tick issued = 0, maturity = 0;
    std::map<Id, int64_t> held;      // corp -> units; NONE = the public
    bool done = false, defaulted = false;
    int missed = 0;                  // days a payment has been overdue
    Tick offerEnds = 0;              // AI issues: the player may buy from the offer until then
    int coupons = 0;                 // coupons fallen due so far
    Money owed = 0;                  // coupons due and not yet paid
    bool principalDue = false;
    template <class A> void ser(A& a) {
        a(id, issuer, units, onOffer, rate30, issued, maturity, held, done, defaulted, missed, offerEnds, coupons, owed, principalDue);
    }
    Money coupon() const { return (Money)std::llround((double)units * BOND_UNIT * rate30); }
    int64_t heldBy(Id c) const { auto it = held.find(c); return it == held.end() ? 0 : it->second; }
};

struct ShortPos {
    Id id = NONE;
    Id corp = NONE, target = NONE;
    int64_t qty = 0;
    Money price = 0, margin = 0, result = 0;
    Tick opened = 0;
    bool open = true;
    template <class A> void ser(A& a) { a(id, corp, target, qty, price, margin, result, opened, open); }
};

struct Underwriting {   // the player's own insurance desk
    bool open = false, licensed = false;
    float rateMul = 1.0f;     // your premium against the Charter underwriters' (0.6 .. 1.5)
    int maxRisk = 50;         // you only cover ships working where the risk is at most this
    Money capacity = 0;       // the most cover you will write in total
    Money premiums = 0, claims = 0;
    std::map<Id, std::pair<Money, Tick>> book;  // ship -> (cover, until)
    int policies = 0, paidClaims = 0;
    template <class A> void ser(A& a) { a(open, licensed, rateMul, maxRisk, capacity, premiums, claims, book, policies, paidClaims); }
    Money exposure() const { Money e = 0; for (auto& [s, c] : book) e += c.first; return e; }
};

bool futuresTradeable(int item);  // minerals, goods and components
inline constexpr int FUTURE_TERMS[3] = {7, 14, 30};
inline constexpr int BOND_TERMS[4] = {30, 60, 90, 180};

// ------------------------------------------------------------------ colonies --
enum CMod : int { CMOD_HABITAT = 0, CMOD_EXTRACTOR, CMOD_SPACEPORT, CMOD_COUNT };
const char* cmodName(int m);
const char* cmodEffect(int m);
struct Colony {
    Id id = NONE;
    Id station = NONE;   // its spaceport: the colony's market and your hangar there
    Id system = NONE;
    int planet = 0;
    PlanetType type = PlanetType::Barren;
    int size = 1;
    float hab = 0, richness = 1;
    Id owner = NONE;
    double pop = 5;      // thousands
    float sat = 1;       // share of its needs met (0..1)
    float unrest = 0;    // days in want
    std::array<uint8_t, CMOD_COUNT> cmod{};
    Money taxes = 0;     // paid to the owner, all time
    Money output = 0;    // goods handed to the owner, all time (value)
    Tick founded = 0;
    std::vector<float> popHist;  // daily, the last 120 days
    template <class A> void ser(A& a) {
        a(id, station, system, planet, type, size, hab, richness, owner, pop, sat, unrest, cmod, taxes, output, founded, popHist);
    }
    double cap() const;  // the population its habitats hold
};

// ------------------------------------------------------------ staff and spies --
enum class StaffReqKind : uint8_t { Raise = 0, Poach, Notice };
struct StaffRequest {
    Id id = NONE;
    StaffReqKind kind = StaffReqKind::Raise;
    Id person = NONE;
    Id rival = NONE;   // poach: who is bidding
    Money wage = 0;    // the wage asked for, or bid
    Tick expires = 0;
    bool open = true;
    template <class A> void ser(A& a) { a(id, kind, person, rival, wage, expires, open); }
};

enum class SpyKind : uint8_t { Dossier = 0, Research, Sabotage, Smear, COUNT };
constexpr int SPY_KIND_COUNT = (int)SpyKind::COUNT;
const char* spyKindName(SpyKind k);
struct SpyOp {
    Id id = NONE;
    Id corp = NONE, target = NONE;
    SpyKind kind = SpyKind::Dossier;
    int bp = -1;          // research theft: the blueprint
    Tick start = 0, end = 0;
    Money cost = 0;
    bool active = true;
    int outcome = 0;      // 1 success, -1 failed quietly, -2 failed and exposed
    std::string note;
    template <class A> void ser(A& a) { a(id, corp, target, kind, bp, start, end, cost, active, outcome, note); }
};

enum class Chan : uint8_t { Corp = 0, Market, Combat, News };
enum class Sev : uint8_t { Info = 0, Good, Warn, Bad };
struct LogEntry {
    Tick t = 0;
    Chan chan = Chan::News;
    Sev sev = Sev::Info;
    bool mine = false;
    Id system = NONE;
    std::string text;
    template <class A> void ser(A& a) { a(t, chan, sev, mine, system, text); }
};

struct PlayerStats {
    int64_t oreUnloaded = 0, refines = 0, sales = 0, ordersPlaced = 0, tradeRuns = 0, manufactured = 0;
    Money bestNetWorth = 0;
    int64_t riskyMinedUnits = 0;  // mined in Contract space or the Frontier
    int64_t contractsDone = 0, outposts = 0;
    int64_t surveys = 0, sitesFound = 0, salvaged = 0, warsWon = 0;
    template <class A> void ser(A& a) {
        a(oreUnloaded, refines, sales, ordersPlaced, tradeRuns, manufactured, bestNetWorth, riskyMinedUnits, contractsDone, outposts,
          surveys, sitesFound, salvaged, warsWon);
    }
};

// ------------------------------------------------------------------ commands --
enum class Cmd : uint8_t {
    MarketBuy, MarketSell, PlaceOrder, CancelOrder,
    CreateFleet, AddShip, RemoveShip, DisbandFleet, SetFleetOrder, RenameFleet,
    Refine, Manufacture, Research, CopyBlueprint,
    BankLoan, RepayLoan, FundRequest, BuyShares, SellShares,
    Hire, Fire, AssignPilot, AssignExec, SetInsurance,
    FitModule, UnfitModule, RenameShip,
    AcceptContract, DeliverContract, PostContract, CancelContract,
    AnchorOutpost, SetOutpostTax,
    GoPublic, DeclareDividend, Buyback, Merge,
    SetLivery, BuyIntel,
    DeclareWar, ProposePact, ProposePeace, RespondOffer, CancelPact, HireMercs,
    SetDivision, AssignDivision, AnswerStaff, StartSpyOp,
    FoundProject, UpgradeStation, ContributeProject,
    SettleColony, UpgradeColony, SupplyColony,
    OpenFuture, CloseFuture, IssueBond, BuyBond, SellBond, OpenShort, CloseShort, AdoptPill, WhiteKnight, PayGreenmail, SetUnderwriting,
};

struct Command {
    Cmd type = Cmd::MarketBuy;
    Id corp = NONE;
    Id a = NONE, b = NONE;
    int item = -1;
    int64_t qty = 0;
    Money price = 0, price2 = 0;
    bool flag = false;
    float f = 0, f2 = 0;
    std::vector<Id> list;
    FleetOrder order;
    Contract contract;
    std::string text;
    DivPolicy policy;
    Underwriting uw;
};

struct Result {
    bool ok = false;
    std::string msg;
    int64_t qty = 0;
    Money value = 0;
    Id id = NONE;
    static Result fail(std::string m) { Result r; r.msg = std::move(m); return r; }
    static Result good(std::string m = {}, int64_t q = 0, Money v = 0) {
        Result r; r.ok = true; r.msg = std::move(m); r.qty = q; r.value = v; return r;
    }
};

struct NewGameSetup {
    uint64_t seed = 1;
    std::string corpName = "Kestrel Orbital";
    std::string ticker = "KORB";
    GalaxySize size = GalaxySize::Standard;
    bool fog = true;    // fog of war: you see what your sensors, allies and bought intel show you
    int livery = 0;
};

// Ship stats with fitted modules applied.
struct ShipStats {
    double cargo = 0, oreHold = 0, mining = 0, attack = 0, hull = 0, minutesPerHex = 0, evasion = 0, scan = 0;
};

// Materials actually used after material-efficiency research (0..10 = 0..10% saved).
int64_t matNeed(int64_t perRun, int64_t runs, int me);
// Minutes to research one level, starting at `level`.
Tick researchMinutes(const BlueprintDef& d, int level);
constexpr double OUTPOST_SHIELD = 20000, OUTPOST_HULL = 30000;
constexpr int64_t OUTPOST_FUEL_PER_DAY = 250;

// --------------------------------------------------------------------- world --
class World {
public:
    static constexpr uint32_t SAVE_MAGIC = 0x5331424E;  // "NB1S"
    static constexpr uint32_t SAVE_VERSION = 4;

    uint64_t seed = 1;
    GalaxySize size = GalaxySize::Standard;
    Rng rng;
    Tick now = 0;
    Id player = 0;
    int hexRadius = 0;

    std::vector<HexCell> cells;
    std::vector<Region> regions;
    std::vector<StarSystem> systems;
    std::vector<Station> stations;
    std::vector<Corp> corps;
    std::vector<Ship> ships;
    std::vector<Fleet> fleets;
    std::vector<Person> people;
    std::vector<Battle> battles;
    std::vector<Job> jobs;
    std::vector<Loan> loans;
    std::vector<LoanRequest> loanReqs;
    std::vector<Contract> contracts;
    std::vector<GalaxyEvent> events;
    std::vector<Site> sites;
    std::vector<Treaty> treaties;
    std::vector<Offer> offers;
    std::vector<Project> projects;
    std::vector<Colony> colonies;
    std::vector<Future> futures;
    std::vector<Bond> bonds;
    std::vector<ShortPos> shorts;
    Underwriting uw;
    std::vector<StaffRequest> staffReqs;
    std::vector<SpyOp> spyOps;
    std::map<Id, Tick> dossiers;     // rival corp -> the player sees its fleets and books until
    // the player's intelligence picture (fog of war)
    bool fog = true;
    std::map<Id, MarketSnap> intel;  // per station
    std::vector<Tick> regionReveal;  // per region: fleet sightings bought until
    std::map<Id, int> surveyed;      // per system: the fortnight it was last surveyed
    std::deque<LogEntry> log;
    int64_t nextOrderId = 1;
    PlayerStats pstats;
    float charterLevy = 0.01f;  // extra sales tax in Charter space

    // derived (rebuilt after generate/load, not saved)
    std::vector<Id> hubs;
    std::vector<Id> regionHub;                    // per region, NONE where it has no hub
    std::vector<std::vector<Id>> regionSystems;   // per region, its systems
    std::vector<Id> outpostIds;                   // every outpost ever anchored (dead ones too)
    std::unordered_map<int64_t, int> cellIndex;   // (q,r) -> cells index
    std::vector<std::vector<Id>> fleetsIn;        // per system, fleets in space there (rebuilt every scan)
    std::vector<uint8_t> sight;                   // per system, the player's sensors: 0 none, 1 sightings, 2 live
    std::vector<Tick> stormUntil;                 // per system, ion storms (from events)
    std::vector<char> crackdown;                  // per region, Warden crackdowns (from events)
    std::array<bool, ITEM_COUNT> embargo{};       // items banned from Charter markets (from events)
    std::vector<DriftEdge> drift;                 // open drift lanes (from sites)
    std::unordered_map<uint64_t, uint8_t> pacts;  // (corp pair) -> live pact bits: 1 war, 2 alliance, 4 non-aggression
    std::unordered_map<uint64_t, char> siteKeys;  // hidden sites already found
    std::unordered_map<uint64_t, Id> gateSet;      // (system pair) -> the jump gate project
    std::vector<std::pair<Id, Id>> gateEnds;       // for the route heuristic
    std::array<double, ITEM_COUNT> dbgTradeProfit{}, dbgTradeTrips{}, dbgMakerPaid{}, dbgMakerReceived{};

    // ---- lifecycle
    void generate(const NewGameSetup& s);
    void tick();
    void advance(int minutes) { for (int i = 0; i < minutes; i++) tick(); }
    bool save(const std::string& path, std::string* err = nullptr) const;
    bool load(const std::string& path, std::string* err = nullptr);
    template <class A> void ser(A& a) {
        a(seed, size, rng, now, player, hexRadius, cells, regions, systems, stations, corps, ships, fleets, people, battles, jobs, loans,
          loanReqs, contracts, events, log, nextOrderId, pstats, charterLevy, sites, treaties, offers, fog, intel, regionReveal, surveyed,
          staffReqs, spyOps, dossiers, projects, colonies, futures, bonds, shorts, uw);
    }

    // ---- the one door for changes
    Result execute(const Command& c);

    // ---- queries
    Corp& pc() { return corps[player]; }
    const Corp& pc() const { return corps[player]; }
    const StarSystem& sysOf(Id station) const { return systems[stations[station].system]; }
    int64_t have(Id corp, Id station, int item) const;
    double hangarVolume(Id corp, Id station) const;
    Money refPrice(Id station, int item) const;
    Money bestAsk(Id station, int item, int64_t* depth = nullptr, Id excludeCorp = NONE) const;
    Money bestBid(Id station, int item, int64_t* depth = nullptr, Id excludeCorp = NONE) const;
    Money valueAt(Id station, int item) const;
    Money valueStock(Id station, int item, int64_t qty) const;
    Money computeNetWorth(Id corp) const;
    ShipStats stats(const Ship& s) const;
    double fleetCargoCap(const Fleet& f) const;
    double fleetOreCap(const Fleet& f) const;
    double fleetCargoUsed(const Fleet& f, bool oreOnly) const;
    double fleetFreeFor(const Fleet& f, int item) const;
    Money fleetCargoValue(const Fleet& f) const;
    double fleetStrength(const Fleet& f) const;
    double fleetEvasion(const Fleet& f) const;
    int laneHexes(Id a, Id b) const;
    double fleetMinutesPerHex(const Fleet& f) const;
    int laneMinutes(const Fleet& f, Id from, Id to) const;
    Money makerQuote(Id station, int item, bool buy, int64_t* depth) const;
    void bookView(Id station, int item, std::vector<Order>& sells, std::vector<Order>& buys) const;
    Money shipValue(const Ship& s) const;
    std::vector<Id> route(Id from, Id to, RoutePolicy p, Id corp = NONE) const;  // corp: may use its drift lanes
    int jumps(Id fromSys, Id toSys) const;               // cached breadth-first distance (999 = unreachable)
    const std::vector<int16_t>& jumpsFrom(Id sys) const; // whole row, cached
    std::vector<Id> systemsWithin(Id sys, int maxJumps, std::vector<int>* dist = nullptr) const;  // breadth-first order
    double pilotFactor(const Ship& s, int skill) const;
    double execBonus(Id corp, int seat) const;
    double refineYield(Id corp, Id station) const;
    double brokerFee(Id corp) const;
    double salesTax(Id corp, Id station = NONE) const;
    Money bankCreditLimit(Id corp) const;
    double bankRate(Id corp) const;
    Money loansOwed(Id corp) const;
    Money insurancePremium(const Ship& s) const;
    double routeRisk(const std::vector<Id>& path, Id from) const;
    Money cargoPremium(Money value, double risk) const { return (Money)(value * (0.002 + 0.0004 * risk)); }
    int wardenRound(Id system) const;  // -1 = no Wardens
    bool isOutlaw(Id corp) const { return corps[corp].charterStanding <= -5.0f; }
    // diplomacy
    static uint64_t pairKey(Id a, Id b) { return a < b ? (uint64_t)(uint32_t)a << 32 | (uint32_t)b : (uint64_t)(uint32_t)b << 32 | (uint32_t)a; }
    uint8_t pactBits(Id a, Id b) const { auto it = pacts.find(pairKey(a, b)); return it == pacts.end() ? 0 : it->second; }
    bool atWar(Id a, Id b) const { return a != b && (pactBits(a, b) & 1); }
    bool allied(Id a, Id b) const { return a != b && (pactBits(a, b) & 2); }
    bool nonAggression(Id a, Id b) const { return a != b && (pactBits(a, b) & 4); }
    Treaty* findTreaty(Pact k, Id a, Id b, bool liveOnly = true);
    Money warFee(Id target) const;
    Money mercQuote(Id merc, int days) const;
    double corpStrength(Id corp) const;  // combat strength of its fleets
    // fog of war
    bool seen(Id system) const { return !fog || (system >= 0 && system < (Id)sight.size() && sight[system] >= 2); }
    bool sighted(Id system) const { return !fog || (system >= 0 && system < (Id)sight.size() && sight[system] >= 1); }
    bool hasSight(Id corp, Id system) const { return !corps[corp].player || seen(system); }
    Money intelPrice(Id region, bool fleetsReport) const;
    // events
    bool stormBlocked(Id system) const { return system >= 0 && system < (Id)stormUntil.size() && stormUntil[system] > now; }
    bool embargoed(Id station, int item) const;
    const GalaxyEvent* eventAt(Id station, int item) const;
    // exploration
    bool beltVisible(const Belt& b, Id corp) const { return b.knownBy == NONE || b.knownBy == corp; }
    int fortnight() const { return (int)(now / (14 * MINUTES_PER_DAY)); }
    bool hostileTo(Id a, Id b) const;
    float standing(Id a, Id b) const;         // a's view of b (archetype default until they deal)
    float defaultStanding(Id a, Id b) const;
    std::vector<Id> recruitHubs() const;
    void addStanding(Id a, Id b, float d);
    std::string fleetLocation(const Fleet& f) const;
    Tick fleetEta(const Fleet& f) const;
    void addLog(Chan c, Sev s, bool mine, Id system, std::string text);
    int cellAt(int q, int r) const;
    bool stationUsable(Id st) const;  // alive and (if an outpost) online
    Money ipoValuation() const;

    // ---- rules (public so the AI and tests can call them)
    void addItem(Id corp, Id station, int item, int64_t qty);
    bool takeItem(Id corp, Id station, int item, int64_t qty);
    void pay(Id corp, Money amount, Cat cat, const std::string& memo);
    Id spawnShip(Id corp, int def, Id station);
    Id createFleet(Id corp, const std::vector<Id>& shipIds, const std::string& name);
    void setOrder(Fleet& f, const FleetOrder& o);
    int64_t instantBuy(Id corp, Id station, int item, int64_t qty, Money maxPrice, Money* spent = nullptr);
    int64_t instantSell(Id corp, Id station, int item, int64_t qty, Money minPrice, Money* got = nullptr);
    Result placeOrder(Id corp, Id station, int item, bool buy, Money price, int64_t qty);
    bool cancelOrder(int64_t orderId, Id corp);
    Result startRefine(Id corp, Id station, int ore, int64_t qty);
    Result startManufacture(Id corp, Id station, int bp, int64_t runs);
    Result startResearch(Id corp, Id station, int bp, JobKind kind, int64_t runs);
    Result takeBankLoan(Id corp, Money amount, int days);
    Result fundRequest(Id corp, Id req);
    Result tradeShares(Id corp, Id target, int64_t qty, bool buy);
    Result fit(Id corp, Id ship, int slot, int moduleItem);
    Result acceptContract(Id corp, Id contract);
    Result deliverContract(Id corp, Id contract);
    Result postContract(Id corp, const Contract& c);
    Result anchorOutpost(Id corp, Id fleet);
    Result goPublic(Id corp, double fraction);
    Result merge(Id corp, Id target);
    Result buyIntel(Id corp, Id region, bool fleetsReport);
    Result declareWar(Id corp, Id target, Id hiredBy = NONE, Id onBehalf = NONE, const std::string& why = "");
    Result proposePact(Id corp, Id target, Pact kind);
    Result proposePeace(Id corp, Id target, Money amount);
    Result respondOffer(Id corp, Id offer, bool accept);
    Result cancelPact(Id corp, Id treaty);
    Result hireMercs(Id corp, Id merc, Id target, int days);
    void destroyShip(Id shipId, Id killerCorp, Battle* b);
    void startBattle(Id system, std::vector<Id> att, std::vector<Id> def);
    void rebuildDerived();
    Id createCorp(Arch a, const std::string& name, const std::string& ticker, Id home, double walletM);
    Id createStation(Id system, StationType t, const std::string& name);
    void setupEconomy(Station& st);

    static int64_t hkey(Id station, int item) { return (int64_t)station * HANGAR_KEY + item; }

    // divisions, staff and espionage (divisions.cpp)
    Result setDivision(Id corp, int div, const DivPolicy& p);
    Result assignDivision(Id corp, Id fleet, int div);
    Result answerStaff(Id corp, Id request, bool yes);
    Result startSpyOp(Id corp, Id target, SpyKind kind, int bp);
    Money spyCost(Id target, SpyKind kind) const;
    double spyChance(Id corp, Id target, SpyKind kind) const;
    Id divisionBase(Id corp, int div) const;
    Person* director(Id corp, int div);
    const Person* director(Id corp, int div) const;
    void divBook(Id corp, int div, Money revenue, Money costs, bool budgeted = false);
    void divFleetBook(const Fleet& f, Money revenue, Money costs);
    Money divBudgetLeft(Id corp, int div) const;
    Money fairWage(const Person& p) const;
    Money oreValue(Id corp, Id station, int ore) const;  // what a unit fetches once refined there
    bool dossierOn(Id target) const { auto it = dossiers.find(target); return it != dossiers.end() && it->second > now; }
    // construction (projects.cpp)
    Result foundProject(Id corp, ProjectKind kind, Id system, Id target, Id station);
    Result upgradeStation(Id corp, Id station, int module);
    Result contributeProject(Id corp, Id project, Money credits, bool goods);
    std::vector<std::pair<int, int64_t>> projectNeeds(ProjectKind kind, int module, int tier) const;
    Money projectCash(ProjectKind kind, int module, int tier) const;
    int projectDays(ProjectKind kind, int tier) const;
    Id projectYard(Id system) const;
    std::string projectWhy(Id corp, ProjectKind kind, Id system, Id target, Id station) const;  // empty = allowed
    bool isGate(Id a, Id b) const { return gateSet.count(pairKey(a, b)) > 0; }
    Money gateToll(const Fleet& f) const;
    double smodBonus(Id station, int module) const;  // tier of the module at a station (0 elsewhere)
    // planets and colonies (colonies.cpp)
    std::vector<Planet> planetsOf(Id system) const;
    bool planetsCharted(Id system) const;  // the player has surveyed them
    Colony* colonyOn(Id system, int planet);
    Colony* colonyAt(Id station);
    const Colony* colonyAt(Id station) const;
    Result settleColony(Id corp, Id fleet, int planet);
    Result upgradeColony(Id corp, Id colony, int module);
    Result supplyColony(Id corp, Id colony);
    std::vector<std::pair<int, double>> colonyNeeds(const Colony& c) const;  // per day
    // capital markets (capital.cpp)
    Money futuresPrice(Id hub, int item, int days) const;
    Result openFuture(Id corp, Id hub, int item, int64_t qty, bool isLong, int days);
    Result closeFuture(Id corp, Id future);
    double bondRate(Id corp, int days) const;
    Money bondCapacity(Id corp) const;  // more face value it can issue
    Result issueBond(Id corp, int64_t units, int days);
    Result buyBond(Id corp, Id bond, int64_t units);
    Result sellBond(Id corp, Id bond, int64_t units);
    Result openShort(Id corp, Id target, int64_t qty);
    Result closeShort(Id corp, Id pos);
    Result adoptPill(Id corp);
    Result whiteKnight(Id corp, Id friendCorp);
    Result payGreenmail(Id corp);
    Result setUnderwriting(Id corp, const Underwriting& u);
    int64_t corpHeldShares(Id target) const;   // shares of target held by corporations
    int64_t lendableShares(Id target) const;   // shares the Exchange will lend to short sellers
    void firePill(Id target, Id raider);
    Money bondsIssued(Id corp) const;          // face value outstanding
    double shipRisk(const Ship& s) const;      // the worst risk on the ship's current work

    // ---- internals (public so tools and tests can reach them; the UI must not call these)
    void genUniverse(const NewGameSetup& s);
    void genStations();
    void genCorps(const NewGameSetup& s);
    Id genPeople(Id hub, int pilots, int execs);  // returns the last person made
    std::string genName(int style);
    std::string genPersonName();
    std::string genCorpName(Arch a, std::string* ticker);
    void hourly();
    void daily();
    void weekly();
    void stationEconomy(Station& st, double hours);
    void makerRepost(Station& st);
    void makerSweep(Station& st);
    void fleetTick(Fleet& f);
    void fleetPlan(Fleet& f);
    void fleetArrive(Fleet& f);
    void beginAct(Fleet& f, Act a, Tick minutes);
    bool goTo(Fleet& f, Id station);
    bool goToSystem(Fleet& f, Id system);
    void unloadAll(Fleet& f, Id station, bool oreOnly = false);
    void repairFleet(Fleet& f);
    void scanForTargets(Fleet& f);
    void siegeTick(Fleet& f);
    void battleTick(Battle& b);
    void endBattle(Battle& b);
    void jobsTick();
    void financeDaily();
    void sharesDaily();
    void peopleDaily();
    void eventsDaily();
    void riskDaily();
    void riskWeekly();
    void contractsHourly();
    void contractsDaily();
    void outpostsHourly();
    void outpostsDaily();
    void corporateDaily();
    // galaxy events (events.cpp)
    void eventsHourly();
    void applyEvent(GalaxyEvent& e, bool on);
    void rebuildEventIndex();
    Id postEvent(EventKind k, Id where, int item, Tick lead, int days, float mag);
    // fog of war and intel (intel.cpp)
    void updateSight();
    void snapshotStation(Id station);
    void intelDaily();
    // exploration (explore.cpp)
    void surveyComplete(Fleet& f);
    void salvageComplete(Fleet& f);
    std::vector<Site> hiddenSites(Id system, int epoch) const;
    Id discoverSite(Id corp, const Site& s);
    void exploreDaily();
    void rebuildDrift();
    void aiExplore(Corp& c);
    // capital markets (capital.cpp)
    void capitalDaily();
    void uwClaim(const Ship& s);  // a ship on your underwriting book was lost
    void aiDefences(Corp& c);
    void aiRaid();
    // colonies (colonies.cpp)
    void coloniesDaily();
    void colonyEconomy(Colony& c);
    void aiColonies();
    // construction (projects.cpp)
    void projectsHourly();
    void projectsDaily();
    void completeProject(Project& p);
    void rebuildGates();
    void payToll(Fleet& f, Id from, Id to);
    void aiProjects();
    void clearJumpCache() { jumpCache_.clear(); }
    // divisions (divisions.cpp)
    void divisionsTick();
    void divisionsDaily();
    void divisionsMonthly();
    void divThink(Corp& c, int div);
    void divMining(Corp& c);
    void divTrade(Corp& c);
    void divHaulage(Corp& c);
    void divIndustry(Corp& c);
    void divSecurity(Corp& c);
    void staffDaily();
    void spyHourly();
    void aiEspionage(Corp& c);
    void resolveSpyOp(SpyOp& op);
    // diplomacy (diplomacy.cpp)
    void rebuildPacts();
    void endTreaty(Treaty& t, const std::string& why);
    void diplomacyHourly();
    void diplomacyDaily();
    void aiDiplomacy(Corp& c);
    void aiWar(Corp& c);
    void noteAggression(Id victim, Id aggressor);
    bool aiAcceptsPact(Id ai, Id other, Pact kind, std::string* why) const;
    bool aiAcceptsPeace(Id ai, Id other, Money amount, std::string* why) const;
    void makePeace(Treaty& war, Money paidByA, const std::string& how);
    void expireOrders();
    void recordTrade(Book& bk, Money price, int64_t qty, bool corpTrade);
    void fill(Id station, int item, Order& o, Id taker, int64_t qty);
    void sortBook(Book& bk);
    Money escrowValue(Id corp) const;
    void markActive(Id system);
    void rebuildFleetIndex();
    void pruneBook(Book& bk);
    void payCargoCover(Fleet& f, Money lostValue);

    // AI
    void aiThink(Corp& c);
    void aiTrader(Corp& c, Fleet& f);
    void aiMiner(Corp& c, Fleet& f);
    void aiIndustry(Corp& c);
    void aiPirate(Corp& c, Fleet& f);
    void aiMercenary(Corp& c, Fleet& f);
    void aiSellHangar(Corp& c, Id station, bool mineralsToo);
    void aiBuyShips(Corp& c);
    void aiFormFleets(Corp& c);
    void aiContracts(Corp& c);
    Id nearestStation(Id system, bool requireStation = true) const;

private:
    // breadth-first distance rows, cached per source (least recently used out)
    mutable std::unordered_map<Id, std::pair<std::vector<int16_t>, uint64_t>> jumpCache_;
    mutable uint64_t jumpClock_ = 0;
};

}  // namespace nb
