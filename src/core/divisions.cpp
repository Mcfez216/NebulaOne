// NebulaOne - divisions, directors and espionage.
//
// The player's company is split into six divisions. Each is run by the
// executive in its seat; with automation on, the director gives the orders to
// the fleets assigned to the division, inside the limits the player sets: a
// base station, a radius in jumps, which zones, how much risk, and a budget.
// Directors report every 30 days, ask for raises after good months, can be
// poached by rivals, and hand in their notice when unhappy.
//
// The Intelligence division runs espionage against rivals (dossiers, research
// theft, sabotage, smear campaigns) and counter-intelligence against theirs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "world.h"

namespace nb {

const char* divisionName(int d) {
    static const char* n[] = {"Mining", "Trade", "Haulage", "Industry", "Security", "Intelligence"};
    return (d >= 0 && d < DIV_COUNT) ? n[d] : "-";
}
int divisionSeat(int d) {
    static const int s[] = {SEAT_MINING, SEAT_TRADE, SEAT_LOGISTICS, SEAT_INDUSTRY, SEAT_SECURITY, SEAT_INTEL};
    return (d >= 0 && d < DIV_COUNT) ? s[d] : -1;
}
int seatDivision(int seat) {
    for (int d = 0; d < DIV_COUNT; d++)
        if (divisionSeat(d) == seat) return d;
    return -1;
}
const char* spyKindName(SpyKind k) {
    switch (k) {
        case SpyKind::Dossier: return "Dossier";
        case SpyKind::Research: return "Research theft";
        case SpyKind::Sabotage: return "Sabotage";
        case SpyKind::Smear: return "Smear campaign";
        default: return "-";
    }
}

namespace {

bool idleFleet(const Fleet& f) {
    if (!f.alive || f.battle != NONE || f.act != Act::Idle) return false;
    if (f.order.kind == OrderKind::None || f.phase == 99) return true;
    if (f.order.kind == OrderKind::Move && f.docked == f.order.station) return true;
    return false;
}

template <class V> void sampleDown(World& w, V& v, size_t n) {
    for (size_t i = 0; i < v.size() && i < n; i++) std::swap(v[i], v[i + (size_t)w.rng.irange(0, (int)(v.size() - i - 1))]);
    if (v.size() > n) v.resize(n);
}

}  // namespace

// ------------------------------------------------------------ helpers ---
Person* World::director(Id corp, int div) {
    int seat = divisionSeat(div);
    if (seat < 0) return nullptr;
    Id p = corps[corp].exec[seat];
    if (p == NONE || p < 0 || p >= (Id)people.size() || !people[p].alive || people[p].employer != corp) return nullptr;
    return &people[p];
}
const Person* World::director(Id corp, int div) const { return const_cast<World*>(this)->director(corp, div); }

Id World::divisionBase(Id corp, int div) const {
    Id b = corps[corp].divs[div].pol.base;
    if (b != NONE && b < (Id)stations.size() && stationUsable(b)) return b;
    return corps[corp].home;
}

Money World::oreValue(Id corp, Id station, int ore) const {
    double v = 0, y = refineYield(corp, station);
    for (int m = 0; m < MINERAL_COUNT; m++) v += item(ore).refine[m] / 100.0 * y * (double)refPrice(station, MINERAL_FIRST + m);
    return (Money)(v * 0.95);
}

Money World::fairWage(const Person& p) const {
    return p.role == Role::Pilot ? credits(2000 + std::max({p.mining, p.nav, p.gunnery}) * 60.0) : credits(15000 + p.mgmt * 250.0);
}

void World::divBook(Id corp, int div, Money revenue, Money costs, bool budgeted) {
    if (div < 0 || div >= DIV_COUNT) return;
    DivisionState& d = corps[corp].divs[div];
    d.revenue += revenue;
    d.costs += costs;
    if (budgeted) d.spent += costs;
}

void World::divFleetBook(const Fleet& f, Money revenue, Money costs) {
    if (f.division < 0 || !corps[f.corp].player) return;
    divBook(f.corp, f.division, revenue, costs);
}

Money World::divBudgetLeft(Id corp, int div) const {
    const DivisionState& d = corps[corp].divs[div];
    if (div == DIV_TRADE) {  // trading capital: what is tied up in cargo right now
        Money tied = 0;
        for (Id fid : corps[corp].fleetList) {
            const Fleet& f = fleets[fid];
            if (f.alive && f.division == DIV_TRADE && f.order.kind == OrderKind::Trade && f.phase == 1) tied += f.spent;
        }
        return std::max<Money>(0, d.pol.budget - tied);
    }
    return std::max<Money>(0, d.pol.budget - d.spent);
}

// --------------------------------------------------------- commands ---
Result World::setDivision(Id corp, int div, const DivPolicy& p) {
    if (div < 0 || div >= DIV_COUNT) return Result::fail("Unknown division.");
    DivPolicy q = p;
    q.radius = std::clamp(q.radius, 1, 12);
    q.maxRisk = std::clamp(q.maxRisk, 0, 100);
    q.lines = std::clamp(q.lines, 1, 4);
    q.budget = std::max<Money>(0, q.budget);
    if (q.zones == 0) q.zones = 1;
    if (q.base != NONE && (q.base < 0 || q.base >= (Id)stations.size() || !stationUsable(q.base))) return Result::fail("That base is not usable.");
    if (q.product >= BP_OUTPOST || q.product < -1) q.product = -1;
    bool was = corps[corp].divs[div].pol.automate;
    corps[corp].divs[div].pol = q;
    corps[corp].divs[div].nextThink = now;
    if (q.automate && !director(corp, div))
        return Result::good(std::string(divisionName(div)) + " settings saved. It needs a director before it can run itself.");
    if (q.automate && !was) return Result::good(std::string(divisionName(div)) + " is now run by " + director(corp, div)->name + ".");
    if (!q.automate && was) return Result::good(std::string(divisionName(div)) + " is back in your hands.");
    return Result::good(std::string(divisionName(div)) + " settings saved.");
}

Result World::assignDivision(Id corp, Id fid, int div) {
    if (fid < 0 || fid >= (Id)fleets.size() || !fleets[fid].alive || fleets[fid].corp != corp) return Result::fail("Unknown fleet.");
    Fleet& f = fleets[fid];
    if (div < -1 || div >= DIV_COUNT || div == DIV_INDUSTRY || div == DIV_INTEL) return Result::fail("That division runs no fleets.");
    if (div == -1) {
        f.division = -1;
        return Result::good(f.name + " now takes its orders from you.");
    }
    if (div == DIV_MINING && fleetOreCap(f) <= 0) return Result::fail("A mining division fleet needs ships with ore holds.");
    if ((div == DIV_TRADE || div == DIV_HAULAGE) && fleetCargoCap(f) < 1000) return Result::fail("Trade and haulage need cargo space (1,000 m3 or more).");
    if (div == DIV_SECURITY && fleetStrength(f) <= 0) return Result::fail("Security fleets need warships.");
    f.division = (int8_t)div;
    corps[corp].divs[div].nextThink = now;
    std::string who = director(corp, div) ? director(corp, div)->name : "the division (no director yet)";
    return Result::good(f.name + " joins " + divisionName(div) + ", run by " + who + ".");
}

Result World::answerStaff(Id corp, Id rid, bool yes) {
    StaffRequest* r = nullptr;
    for (auto& x : staffReqs)
        if (x.id == rid) r = &x;
    if (!r || !r->open || r->expires <= now) return Result::fail("That is no longer on the table.");
    Person& p = people[r->person];
    if (p.employer != corp) return Result::fail("They no longer work for you.");
    r->open = false;
    switch (r->kind) {
        case StaffReqKind::Raise:
            if (yes) {
                p.wage = r->wage;
                p.loyalty = std::min(100.0f, p.loyalty + 12);
                return Result::good(p.name + " thanks you: wage now " + fmtMoney(p.wage) + " a day.");
            }
            p.loyalty = std::max(0.0f, p.loyalty - 15);
            return Result::good(p.name + " takes the news badly (loyalty " + std::to_string((int)p.loyalty) + ").");
        case StaffReqKind::Poach:
        case StaffReqKind::Notice:
            if (yes) {
                p.wage = std::max(p.wage, r->wage);
                p.loyalty = std::max(p.loyalty, 55.0f) + 10;
                return Result::good(p.name + " stays, at " + fmtMoney(p.wage) + " a day.");
            }
            r->expires = now;  // let them go now
            r->open = true;
            return Result::good(p.name + " will clear their desk.");
    }
    return Result::fail("Unknown request.");
}

// ----------------------------------------------------------- espionage ---
Money World::spyCost(Id target, SpyKind kind) const {
    double nw = std::max(0.0, (double)corps[target].netWorth);
    switch (kind) {
        case SpyKind::Dossier: return (Money)(250000.0 * CR + nw * 0.0005);
        case SpyKind::Research: return 1500000 * CR;
        case SpyKind::Sabotage: return 2000000 * CR;
        case SpyKind::Smear: return (Money)(3000000.0 * CR + nw * 0.001);
        default: return 0;
    }
}

static double defenceOf(const World& w, Id corp) {
    const Corp& c = w.corps[corp];
    if (!c.player) return c.aiSkill / 100.0;
    double d = 0.15;
    if (const Person* p = w.director(corp, DIV_INTEL)) d += p->mgmt / 100.0 * (c.divs[DIV_INTEL].pol.automate ? 0.75 : 0.45);
    d += 0.25 * w.execBonus(corp, SEAT_SECURITY);
    return d;
}

static double attackOf(const World& w, Id corp) {
    const Corp& c = w.corps[corp];
    if (!c.player) return c.aiSkill / 100.0;
    const Person* p = w.director(corp, DIV_INTEL);
    return p ? p->mgmt / 100.0 * (p->seat == SEAT_INTEL ? 1.1 : 1.0) : 0.0;
}

double World::spyChance(Id corp, Id target, SpyKind kind) const {
    static const double base[] = {0.45, 0.05, 0.0, 0.10};
    double c = 0.25 + base[(int)kind] + 0.5 * attackOf(*this, corp) - 0.45 * defenceOf(*this, target);
    return std::clamp(c, 0.05, 0.92);
}

Result World::startSpyOp(Id corp, Id target, SpyKind kind, int bp) {
    if (target < 0 || target >= (Id)corps.size() || target == corp || !corps[target].alive) return Result::fail("Pick a rival.");
    Corp& me = corps[corp];
    if (me.player && !director(corp, DIV_INTEL)) return Result::fail("Espionage needs an Intelligence Director.");
    for (auto& op : spyOps)
        if (op.active && op.corp == corp && op.target == target) return Result::fail("An operation against " + corps[target].name + " is already running.");
    if (kind == SpyKind::Research) {
        if (bp < 0 || bp >= BLUEPRINT_COUNT) return Result::fail("Pick a blueprint to steal.");
        if (corps[target].me[bp] <= me.me[bp] && corps[target].te[bp] <= me.te[bp])
            return Result::fail(corps[target].name + " knows nothing about that blueprint you do not.");
    }
    if (kind == SpyKind::Smear && !corps[target].player && corps[target].sharesOut <= 0) return Result::fail("They have no shares to smear.");
    Money cost = spyCost(target, kind);
    if (me.wallet < cost) return Result::fail("The operation costs " + fmtMoney(cost) + ".");
    if (me.player && corps[corp].divs[DIV_INTEL].pol.budget > 0 && cost > divBudgetLeft(corp, DIV_INTEL))
        return Result::fail("That is over the Intelligence budget left this month (" + fmtMoneyShort(divBudgetLeft(corp, DIV_INTEL)) + ").");
    static const int days[] = {2, 6, 4, 5};
    SpyOp op;
    op.id = (Id)spyOps.size();
    op.corp = corp;
    op.target = target;
    op.kind = kind;
    op.bp = bp;
    op.start = now;
    op.end = now + days[(int)kind] * MINUTES_PER_DAY;
    op.cost = cost;
    spyOps.push_back(op);
    pay(corp, -cost, Cat::Espionage, std::string(spyKindName(kind)) + ": " + corps[target].name);
    if (me.player) divBook(corp, DIV_INTEL, 0, cost, true);
    Result r = Result::good(std::string(spyKindName(kind)) + " against " + corps[target].name + " under way; results in " +
                            std::to_string(days[(int)kind]) + " days. Odds about " + std::to_string((int)std::round(spyChance(corp, target, kind) * 100)) + "%.");
    r.id = op.id;
    return r;
}

void World::resolveSpyOp(SpyOp& op) {
    op.active = false;
    Corp& me = corps[op.corp];
    Corp& them = corps[op.target];
    bool mine = me.player, onMe = them.player;
    if (!them.alive) { op.outcome = -1; op.note = "The target no longer exists."; return; }
    double chance = spyChance(op.corp, op.target, op.kind);
    if (rng.chance(chance)) {
        op.outcome = 1;
        switch (op.kind) {
            case SpyKind::Dossier:
                if (mine) dossiers[op.target] = now + 10 * MINUTES_PER_DAY;
                op.note = "Dossier open for 10 days: fleets, books and finances.";
                break;
            case SpyKind::Research: {
                int bp = op.bp;
                std::string what;
                if (them.me[bp] > me.me[bp]) { me.me[bp] = (uint8_t)std::min<int>(them.me[bp], me.me[bp] + 2); what += "ME " + std::to_string(me.me[bp]); }
                if (them.te[bp] > me.te[bp]) {
                    me.te[bp] = (uint8_t)std::min<int>(them.te[bp], me.te[bp] + 2);
                    what += (what.empty() ? "" : ", ") + std::string("TE ") + std::to_string(me.te[bp]);
                }
                op.note = item(blueprint(bp).product).name + " research stolen: " + (what.empty() ? "nothing new" : what) + ".";
                break;
            }
            case SpyKind::Sabotage: {
                int hit = 0;
                for (auto& j : jobs)
                    if (j.active && j.corp == op.target && j.kind != JobKind::Refine) { j.end += 3 * MINUTES_PER_DAY; hit++; }
                for (Id sid : outpostIds) {
                    Station& o = stations[sid];
                    if (o.dead || o.owner != op.target || o.state != OutpostState::Online) continue;
                    o.shield *= 0.5;
                    hit++;
                    break;
                }
                op.note = hit ? "Sabotage worked: " + std::to_string(hit) + " jobs and works set back." : "Sabotage worked, but there was little to break.";
                break;
            }
            case SpyKind::Smear:
                them.smear = std::min(0.4f, them.smear + (float)rng.range(0.12, 0.25));
                them.sharePrice = std::max<Money>(1, (Money)(them.sharePrice * (1.0 - them.smear * 0.5)));
                if (them.player && them.listed) them.board = std::max(0.0f, them.board - 8);
                op.note = "The story ran: " + them.name + " shares marked down.";
                break;
            default: break;
        }
        if (mine) addLog(Chan::Corp, Sev::Good, true, NONE, std::string(spyKindName(op.kind)) + " against " + them.name + ": " + op.note);
        if (onMe) {
            // a successful operation against you is noticed later, without a name on it
            const char* what = op.kind == SpyKind::Research ? "Someone has copied your research." :
                               op.kind == SpyKind::Sabotage ? "Sabotage: your jobs and works have been set back." :
                               op.kind == SpyKind::Smear ? "A smear campaign is hitting your share price." : nullptr;
            if (what) addLog(Chan::Corp, Sev::Bad, true, NONE, std::string(what) + " Your Intelligence division could not name the culprit.");
        }
        return;
    }
    bool exposed = rng.chance(op.kind == SpyKind::Dossier ? 0.3 : 0.6);
    op.outcome = exposed ? -2 : -1;
    op.note = exposed ? "Failed, and the agents were caught." : "Failed quietly.";
    if (exposed) {
        addStanding(op.target, op.corp, -4);
        me.charterStanding -= 0.5f;
        noteAggression(op.target, op.corp);
        addLog(Chan::News, Sev::Warn, mine || onMe, NONE,
               them.name + " caught agents of " + me.name + " attempting " + std::string(spyKindName(op.kind)) + "; the Charter Authority takes note.");
    }
    if (mine && !exposed) addLog(Chan::Corp, Sev::Warn, true, NONE, std::string(spyKindName(op.kind)) + " against " + them.name + " failed quietly.");
    if (onMe && exposed) addLog(Chan::Corp, Sev::Good, true, NONE, "Counter-intelligence caught " + me.name + " spying on you.");
}

void World::spyHourly() {
    for (auto& op : spyOps)
        if (op.active && op.end <= now) resolveSpyOp(op);
    if (spyOps.size() > 300) {
        std::vector<SpyOp> keep;
        for (auto& op : spyOps)
            if (op.active || op.end > now - 60 * MINUTES_PER_DAY) keep.push_back(op);
        spyOps = keep;
        for (size_t i = 0; i < spyOps.size(); i++) spyOps[i].id = (Id)i;
    }
}

// AI corporations spy on the player: research theft when the player is ahead,
// sabotage when the player is building, smear campaigns once the player is listed.
void World::aiEspionage(Corp& c) {
    if (c.arch == Arch::Pirate || c.arch == Arch::Ganker || c.wallet < 30000000 * CR) return;
    Id t = player;
    if (standing(c.id, t) > 1) return;
    for (auto& op : spyOps)
        if (op.active && op.corp == c.id) return;
    std::vector<SpyKind> kinds;
    int bestBp = -1;
    for (int bp = 0; bp < BLUEPRINT_COUNT; bp++)
        if (corps[t].me[bp] > c.me[bp] + 1 || corps[t].te[bp] > c.te[bp] + 1) bestBp = bp;
    if (bestBp >= 0 && c.arch == Arch::Industrialist) kinds.push_back(SpyKind::Research);
    bool building = false;
    for (auto& j : jobs) building = building || (j.active && j.corp == t && j.kind == JobKind::Manufacture);
    if (building && standing(c.id, t) < -2) kinds.push_back(SpyKind::Sabotage);
    if (corps[t].listed && (c.arch == Arch::Trader || c.arch == Arch::Industrialist)) kinds.push_back(SpyKind::Smear);
    if (kinds.empty()) return;
    SpyKind k = kinds[rng.irange(0, (int)kinds.size() - 1)];
    startSpyOp(c.id, t, k, k == SpyKind::Research ? bestBp : -1);
}

// ---------------------------------------------------------- the staff ---
void World::staffDaily() {
    Corp& me = pc();
    // requests that ran out
    for (auto& r : staffReqs) {
        if (!r.open || r.expires > now) continue;
        r.open = false;
        Person& p = people[r.person];
        if (p.employer != player) continue;
        if (r.kind == StaffReqKind::Raise) {
            p.loyalty = std::max(0.0f, p.loyalty - 15);
            addLog(Chan::Corp, Sev::Warn, true, NONE, p.name + " heard nothing about the raise they asked for (loyalty " + std::to_string((int)p.loyalty) + ").");
            continue;
        }
        // poached, or gone
        for (auto& e : me.exec)
            if (e == p.id) e = NONE;
        if (r.kind == StaffReqKind::Poach && r.rival != NONE && corps[r.rival].alive) {
            p.employer = r.rival;
            corps[r.rival].aiSkill = std::min(95.0f, corps[r.rival].aiSkill + p.mgmt / 25.0f);
            addLog(Chan::News, Sev::Bad, true, NONE, p.name + " leaves " + me.name + " for " + corps[r.rival].name + ".");
        } else {
            p.employer = NONE;
            p.alive = false;
            addLog(Chan::Corp, Sev::Bad, true, NONE, p.name + " has left the company.");
        }
    }
    if (staffReqs.size() > 80) {
        std::vector<StaffRequest> keep;
        for (auto& r : staffReqs)
            if (r.open) keep.push_back(r);
        staffReqs = keep;
        for (size_t i = 0; i < staffReqs.size(); i++) staffReqs[i].id = (Id)i;
    }
    auto pending = [&](Id person) {
        for (auto& r : staffReqs)
            if (r.open && r.person == person) return true;
        return false;
    };
    // rivals try to poach your best people
    for (int s = 0; s < SEAT_COUNT; s++) {
        Id pid = me.exec[s];
        if (pid == NONE || pending(pid)) continue;
        Person& p = people[pid];
        if (now - p.hired < 20 * MINUTES_PER_DAY) continue;
        double chance = 0.004 * (p.mgmt / 70.0) * (1.4 - p.loyalty / 100.0);
        if (!rng.chance(chance)) continue;
        Id rival = NONE;
        Money best = 0;
        for (auto& c : corps)
            if (c.alive && !c.player && (c.arch == Arch::Trader || c.arch == Arch::Industrialist || c.arch == Arch::Miner) && c.netWorth > best &&
                rng.chance(0.3)) {
                best = c.netWorth;
                rival = c.id;
            }
        if (rival == NONE) continue;
        StaffRequest r;
        r.id = (Id)staffReqs.size();
        r.kind = StaffReqKind::Poach;
        r.person = pid;
        r.rival = rival;
        r.wage = std::max(p.wage, fairWage(p)) * (Money)rng.irange(125, 160) / 100 / 1000 * 1000;
        r.expires = now + 3 * MINUTES_PER_DAY;
        staffReqs.push_back(r);
        addLog(Chan::Corp, Sev::Warn, true, NONE, corps[rival].name + " has offered " + p.name + " (" + seatName(s) + ") " + fmtMoney(r.wage) +
                                                     " a day. Match it within 3 days or lose them.");
    }
    // the unhappy hand in their notice
    for (int s = 0; s < SEAT_COUNT; s++) {
        Id pid = me.exec[s];
        if (pid == NONE || pending(pid) || people[pid].loyalty >= 20) continue;
        Person& p = people[pid];
        if (!rng.chance(0.25)) continue;
        StaffRequest r;
        r.id = (Id)staffReqs.size();
        r.kind = StaffReqKind::Notice;
        r.person = pid;
        r.wage = std::max(p.wage, fairWage(p)) * 120 / 100 / 1000 * 1000;
        r.expires = now + 7 * MINUTES_PER_DAY;
        staffReqs.push_back(r);
        addLog(Chan::Corp, Sev::Bad, true, NONE, p.name + " (" + seatName(s) + ") has handed in their notice: 7 days, unless you pay " + fmtMoney(r.wage) + " a day.");
    }
}

// ----------------------------------------------------- the divisions ---
void World::divisionsTick() {
    Corp& c = pc();
    for (int d = 0; d < DIV_COUNT; d++) {
        DivisionState& ds = c.divs[d];
        if (!ds.pol.automate || now < ds.nextThink) continue;
        const Person* p = director(c.id, d);
        if (!p) { ds.status = "No director: the division waits"; ds.nextThink = now + 60; continue; }
        // better directors look again sooner
        ds.nextThink = now + (Tick)std::max(20.0, 75.0 - p->mgmt * 0.5);
        divThink(c, d);
    }
}

// Directors crew their own fleets: idle pilots first, then the best candidate on the market.
static void crewFleet(World& w, Corp& c, Fleet& f, int div) {
    for (Id sid : f.ships) {
        Ship& s = w.ships[sid];
        if (!s.alive || s.pilot != NONE) continue;
        auto skill = [&](const Person& p) {
            return div == DIV_MINING ? p.mining : div == DIV_SECURITY ? p.gunnery : p.nav;
        };
        Id best = NONE;
        int bestSkill = -1;
        for (auto& p : w.people)
            if (p.alive && p.employer == c.id && p.role == Role::Pilot && p.ship == NONE && p.injuredUntil <= w.now && skill(p) > bestSkill) {
                best = p.id;
                bestSkill = skill(p);
            }
        if (best == NONE) {
            double bestV = -1;
            for (auto& p : w.people)
                if (p.alive && p.employer == NONE && p.hub != NONE && p.role == Role::Pilot && p.availableUntil > w.now) {
                    double v = skill(p) / std::max(1.0, toCr(p.wage) / 1000.0);
                    if (v > bestV) { bestV = v; best = p.id; }
                }
            if (best == NONE) { f.status = "Waiting for pilots: none on the market"; return; }
            Command h;
            h.type = Cmd::Hire;
            h.corp = c.id;
            h.a = best;
            Result r = w.execute(h);
            if (!r.ok) { f.status = "Waiting for pilots: " + r.msg; return; }
            w.divBook(c.id, div, 0, w.people[best].wage * 3);
        }
        Command a;
        a.type = Cmd::AssignPilot;
        a.corp = c.id;
        a.a = best;
        a.b = sid;
        w.execute(a);
    }
}

void World::divThink(Corp& c, int d) {
    if (d != DIV_INDUSTRY && d != DIV_INTEL)
        for (Id fid : std::vector<Id>(c.fleetList))
            if (fleets[fid].alive && fleets[fid].division == d) crewFleet(*this, c, fleets[fid], d);
    switch (d) {
        case DIV_MINING: divMining(c); break;
        case DIV_TRADE: divTrade(c); break;
        case DIV_HAULAGE: divHaulage(c); break;
        case DIV_INDUSTRY: divIndustry(c); break;
        case DIV_SECURITY: divSecurity(c); break;
        case DIV_INTEL: {
            int running = 0;
            for (auto& op : spyOps) running += op.active && op.corp == c.id;
            c.divs[d].status = running ? std::to_string(running) + " operations running; counter-intelligence on watch" : "Counter-intelligence on watch";
            break;
        }
    }
}

// How much a director's judgement wobbles: a great one picks the best option; a poor one often does not.
static double judgement(World& w, const Person& p) {
    double spread = 0.45 * (1.0 - p.mgmt / 100.0);
    return w.rng.range(1.0 - spread, 1.0 + spread);
}

static bool systemAllowed(const World& w, const DivPolicy& pol, Id sy) {
    const StarSystem& s = w.systems[sy];
    int bit = s.zone == Zone::Charter ? 1 : s.zone == Zone::Contract ? 2 : 4;
    if (!(pol.zones & bit)) return false;
    // the worse of the published rating and today's figure, and no system with a fight going on
    if (std::max(s.risk, s.riskLive) > pol.maxRisk || w.stormBlocked(sy) || s.heat > 2.5f) return false;
    return true;
}

static RoutePolicy routeFor(const DivPolicy& pol) {
    if (!(pol.zones & 2) || pol.maxRisk < 40) return RoutePolicy::LowestRisk;  // Charter only: keep out of Contract space too
    if (!(pol.zones & 4)) return RoutePolicy::AvoidFrontier;
    return RoutePolicy::LowestRisk;
}

// Every system on the way must be inside the division's limits too, not just the ends.
static bool routeOk(World& w, Id corp, Id from, Id to, const DivPolicy& pol) {
    if (from == to) return true;
    std::vector<Id> path = w.route(from, to, routeFor(pol), corp);
    if (path.empty()) return false;
    for (Id sy : path)
        if (!systemAllowed(w, pol, sy)) return false;
    return true;
}

void World::divMining(Corp& c) {
    DivisionState& ds = c.divs[DIV_MINING];
    const DivPolicy& pol = ds.pol;
    const Person& dir = *director(c.id, DIV_MINING);
    Id base = divisionBase(c.id, DIV_MINING);
    Id baseSys = stations[base].system;
    std::vector<int> nd;
    std::vector<Id> near = systemsWithin(baseSys, pol.radius, &nd);
    std::set<Id> unloadAt;
    int working = 0, idle = 0, fleetsN = 0;
    for (Id fid : std::vector<Id>(c.fleetList)) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.division != DIV_MINING) continue;
        fleetsN++;
        if (f.order.kind == OrderKind::Mine && f.order.station != NONE) unloadAt.insert(f.order.station);
        // a mining order into a system that has turned too risky gets called off
        if (f.order.kind == OrderKind::Mine && f.order.system != NONE && !systemAllowed(*this, pol, f.order.system) && f.battle == NONE) {
            FleetOrder o;
            o.kind = OrderKind::Move;
            o.station = f.order.station != NONE ? f.order.station : base;
            o.policy = RoutePolicy::LowestRisk;
            setOrder(f, o);
            continue;
        }
        if (!idleFleet(f)) { working++; continue; }
        idle++;
        double rate = 0;
        for (Id s : f.ships) rate += stats(ships[s]).mining;
        double bestScore = 0;
        Id bSys = NONE;
        int bBelt = -1;
        for (size_t k = 0; k < near.size(); k++) {
            Id sy = near[k];
            if (!systemAllowed(*this, pol, sy)) continue;
            const StarSystem& s = systems[sy];
            int crowd = 0;
            for (Id gid : c.fleetList) crowd += gid != fid && fleets[gid].alive && fleets[gid].order.kind == OrderKind::Mine && fleets[gid].order.system == sy;
            for (int b = 0; b < (int)s.belts.size(); b++) {
                const Belt& bl = s.belts[b];
                if (bl.capacity <= 0 || !beltVisible(bl, c.id) || bl.remaining < std::min(bl.capacity * 0.15, 50000.0)) continue;
                if (bl.until && bl.until < now + 12 * MINUTES_PER_HOUR) continue;
                double perfect = 0;
                for (int m = 0; m < MINERAL_COUNT; m++) perfect += item(bl.ore).refine[m] / 100.0 * (double)refPrice(base, MINERAL_FIRST + m);
                double score = perfect * std::max(1.0, rate) / (1.0 + 0.2 * nd[k]) / (1.0 + crowd) / (1.0 + s.risk / 80.0) * judgement(*this, dir);
                if (bl.knownBy == c.id) score *= 1.3;  // our own pocket
                if (score > bestScore) { bestScore = score; bSys = sy; bBelt = b; }
            }
        }
        if (bSys == NONE) { f.status = "No belt within the division's limits"; continue; }
        Id at = f.docked != NONE ? stations[f.docked].system : f.system;
        if (!routeOk(*this, c.id, at, bSys, pol)) { f.status = "The way to the best belt runs outside the limits"; continue; }
        // unload at the nearest usable station within limits, the base on a tie
        Id unload = NONE;
        int bd = 99;
        std::vector<int> ud;
        std::vector<Id> unear = systemsWithin(bSys, 4, &ud);
        for (size_t k = 0; k < unear.size(); k++) {
            if (!systemAllowed(*this, pol, unear[k]) && unear[k] != baseSys) continue;
            for (Id st : systems[unear[k]].stations) {
                if (!stationUsable(st) || (stations[st].isOutpost() && stations[st].owner != c.id && !(pol.zones & 4))) continue;
                int dd = ud[k];
                if (dd < bd || (dd == bd && st == base)) { bd = dd; unload = st; }
            }
        }
        if (unload == NONE) unload = base;
        FleetOrder o;
        o.kind = OrderKind::Mine;
        o.system = bSys;
        o.belt = bBelt;
        o.station = unload;
        o.policy = routeFor(pol);
        setOrder(f, o);
        unloadAt.insert(unload);
    }
    // refine what the fleets brought in, and sell the minerals if told to
    for (Id st : unloadAt) {
        if (!stationUsable(st)) continue;
        for (int ore = ORE_FIRST; ore < ORE_FIRST + ORE_COUNT; ore++) {
            int64_t q = have(c.id, st, ore) / 100 * 100;
            if (q < 100) continue;
            Money before = c.wallet;
            if (startRefine(c.id, st, ore, q).ok) {
                for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
                    if (it->corp == c.id && it->kind == JobKind::Refine && it->start == now) { it->division = DIV_MINING; break; }
                divBook(c.id, DIV_MINING, 0, before - c.wallet);
            }
        }
        if (!pol.sell) continue;
        for (int m = MINERAL_FIRST; m < GOOD_FIRST; m++) {
            int64_t q = have(c.id, st, m);
            if (q <= 0) continue;
            Money ref = refPrice(st, m);
            instantSell(c.id, st, m, q, ref * 92 / 100);
            int64_t left = have(c.id, st, m);
            if (left <= 0) continue;
            bool listed = false;
            if (const Book* bk = stations[st].findBook(m))
                for (auto& o : bk->sells) listed = listed || o.corp == c.id;
            if (!listed) placeOrder(c.id, st, m, false, ref * 97 / 100, left);
        }
    }
    ds.status = fleetsN == 0 ? "No fleets: assign mining fleets to the division"
                             : std::to_string(working + idle) + " fleets mining within " + std::to_string(pol.radius) + " jumps of " + stations[base].name;
}

void World::divTrade(Corp& c) {
    DivisionState& ds = c.divs[DIV_TRADE];
    const DivPolicy& pol = ds.pol;
    const Person& dir = *director(c.id, DIV_TRADE);
    Id base = divisionBase(c.id, DIV_TRADE);
    Id baseSys = stations[base].system;
    std::vector<int> bd;
    std::vector<Id> area = systemsWithin(baseSys, pol.radius, &bd);
    std::set<Id> inArea(area.begin(), area.end());
    // cargo that did not sell on arrival: sell it where it is, or list it
    {
        std::vector<int64_t> keep;
        for (int64_t key : ds.leftovers) {
            Id st = (Id)(key / HANGAR_KEY);
            int it = (int)(key % HANGAR_KEY);
            int64_t q = have(c.id, st, it);
            if (q <= 0 || !stationUsable(st)) continue;
            Money ref = refPrice(st, it), got = 0;
            instantSell(c.id, st, it, q, ref * 88 / 100, &got);
            divBook(c.id, DIV_TRADE, (Money)(got * (1.0 - salesTax(c.id, st))), 0);
            int64_t left = have(c.id, st, it);
            if (left <= 0) continue;
            bool listed = false;
            if (const Book* bk = stations[st].findBook(it))
                for (auto& o : bk->sells) listed = listed || o.corp == c.id;
            if (!listed) placeOrder(c.id, st, it, false, ref * 96 / 100, left);
            if (std::find(keep.begin(), keep.end(), key) == keep.end()) keep.push_back(key);
        }
        ds.leftovers = keep;
    }
    int running = 0, fleetsN = 0;
    for (Id fid : std::vector<Id>(c.fleetList)) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.division != DIV_TRADE) continue;
        fleetsN++;
        if (!idleFleet(f)) { running++; continue; }
        if (f.docked == NONE && f.system != NONE && !inArea.count(f.system)) {
            FleetOrder o;
            o.kind = OrderKind::Move;
            o.station = base;
            o.policy = routeFor(pol);
            setOrder(f, o);
            continue;
        }
        Money budget = std::min(divBudgetLeft(c.id, DIV_TRADE), std::max<Money>(0, c.wallet / 2));
        double cap = fleetCargoCap(f);
        if (cap <= 0 || budget < 100000 * CR) { f.status = "Waiting for trading capital"; continue; }
        Id loc = f.docked != NONE ? stations[f.docked].system : f.system;
        std::vector<Id> srcs, dsts;
        for (Id sy : systemsWithin(loc, 3))
            if (inArea.count(sy) && systemAllowed(*this, pol, sy))
                for (Id st : systems[sy].stations)
                    if (stationUsable(st)) srcs.push_back(st);
        for (Id sy : area)
            if (systemAllowed(*this, pol, sy))
                for (Id st : systems[sy].stations)
                    if (stationUsable(st)) dsts.push_back(st);
        size_t look = 8 + (size_t)(dir.mgmt / 10);  // better directors check more markets
        sampleDown(*this, srcs, look);
        sampleDown(*this, dsts, look * 3);
        if (srcs.empty() || dsts.size() < 2) { f.status = "No markets within the limits"; continue; }
        double mph = fleetMinutesPerHex(f);
        struct Cand { double score; Id A, B; int item; int64_t qty; Money ask, bid; };
        std::vector<Cand> cands;
        for (Id A : srcs) {
            Id sa = stations[A].system;
            int dA = hexDistance(systems[loc].hex, systems[sa].hex);
            for (int it = MINERAL_FIRST; it < SHIP_FIRST; it++) {
                int64_t depthA = 0;
                Money ask = bestAsk(A, it, &depthA, c.id);
                if (!ask || depthA <= 0) continue;
                double vol = item(it).volume;
                for (Id B : dsts) {
                    if (B == A) continue;
                    int64_t depthB = 0;
                    Money bb = bestBid(B, it, &depthB, c.id);
                    if (bb <= ask * 106 / 100) continue;
                    int64_t q = std::min({depthA, depthB, (int64_t)(cap / vol), (int64_t)(budget / ask)});
                    if (q <= 0) continue;
                    double profit = q * ((double)bb * (1.0 - salesTax(c.id, B)) - (double)ask);
                    int dAB = hexDistance(systems[sa].hex, systems[stations[B].system].hex);
                    double minutes = (dA + dAB) * 1.35 * mph + 45.0 + (systems[stations[B].system].zone != Zone::Charter ? 60.0 : 0.0);
                    double score = profit / minutes * judgement(*this, dir);
                    if (score * 60 >= 15000.0 * CR) cands.push_back({score, A, B, it, q, ask, bb});
                }
            }
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score > y.score; });
        Id bA = NONE, bB = NONE;
        int bItem = -1;
        int64_t bQty = 0;
        Money bAsk = 0, bBid = 0;
        for (size_t k = 0; k < cands.size() && k < 6; k++) {
            const Cand& x = cands[k];
            if (!routeOk(*this, c.id, loc, stations[x.A].system, pol) || !routeOk(*this, c.id, stations[x.A].system, stations[x.B].system, pol)) continue;
            bA = x.A; bB = x.B; bItem = x.item; bQty = x.qty; bAsk = x.ask; bBid = x.bid;
            break;
        }
        if (bA == NONE) {
            f.status = "Nothing worth carrying right now";
            if (f.docked != base) {
                FleetOrder o;
                o.kind = OrderKind::Move;
                o.station = base;
                o.policy = routeFor(pol);
                setOrder(f, o);
            }
            continue;
        }
        FleetOrder o;
        o.kind = OrderKind::Trade;
        o.station = bA;
        o.station2 = bB;
        o.item = bItem;
        o.qty = bQty;
        o.price = bAsk * 103 / 100;
        o.price2 = bBid * 94 / 100;
        o.policy = routeFor(pol);
        o.insureCargo = pol.insure;
        setOrder(f, o);
        running++;
    }
    ds.status = fleetsN == 0 ? "No fleets: assign haulers to the division"
                             : std::to_string(running) + " of " + std::to_string(fleetsN) + " fleets on trade runs; " +
                                   fmtMoneyShort(divBudgetLeft(c.id, DIV_TRADE)) + " capital free";
}

void World::divHaulage(Corp& c) {
    DivisionState& ds = c.divs[DIV_HAULAGE];
    const DivPolicy& pol = ds.pol;
    Id base = divisionBase(c.id, DIV_HAULAGE);
    Id baseSys = stations[base].system;
    std::vector<int> bd;
    std::vector<Id> area = systemsWithin(baseSys, pol.radius, &bd);
    std::set<Id> inArea(area.begin(), area.end());
    std::set<Id> claimedStations;  // one fleet per job
    for (Id fid : c.fleetList) {
        const Fleet& g = fleets[fid];
        if (g.alive && g.division == DIV_HAULAGE && g.order.kind == OrderKind::Haul && !idleFleet(g)) {
            claimedStations.insert(g.order.station);
            claimedStations.insert(g.order.station2);
        }
    }
    int busy = 0, fleetsN = 0;
    std::string last;
    for (Id fid : std::vector<Id>(c.fleetList)) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.division != DIV_HAULAGE) continue;
        fleetsN++;
        if (!idleFleet(f)) { busy++; continue; }
        double cap = fleetCargoCap(f);
        bool done = false;
        // 1. outposts running low on fuel
        if (pol.fuel)
            for (Id oid : outpostIds) {
                const Station& o = stations[oid];
                if (o.dead || o.owner != c.id || o.state == OutpostState::Anchoring || claimedStations.count(oid)) continue;
                int64_t fuel = have(c.id, oid, FUEL_CELLS);
                if (fuel >= OUTPOST_FUEL_PER_DAY * 6) continue;
                int64_t want = std::min<int64_t>(OUTPOST_FUEL_PER_DAY * 20 - fuel, (int64_t)(cap / item(FUEL_CELLS).volume));
                // the nearest market selling fuel
                Id src = NONE;
                Money px = 0;
                for (Id sy : systemsWithin(o.system, 8)) {
                    for (Id st : systems[sy].stations) {
                        if (st == oid || !stationUsable(st)) continue;
                        int64_t depth = 0;
                        Money ask = bestAsk(st, FUEL_CELLS, &depth, c.id);
                        if (ask && depth >= want / 2) { src = st; px = ask; break; }
                    }
                    if (src != NONE) break;
                }
                if (src == NONE) continue;
                int64_t stock = have(c.id, src, FUEL_CELLS);
                if (stock < want) {
                    Money left = pol.budget > 0 ? divBudgetLeft(c.id, DIV_HAULAGE) : c.wallet / 4;
                    int64_t buy = std::min<int64_t>(want - stock, left / std::max<Money>(1, px));
                    Money spent = 0;
                    if (buy > 0) instantBuy(c.id, src, FUEL_CELLS, buy, px * 12 / 10, &spent);
                    divBook(c.id, DIV_HAULAGE, 0, spent, true);
                }
                if (have(c.id, src, FUEL_CELLS) <= 0) continue;
                FleetOrder h;
                h.kind = OrderKind::Haul;
                h.station = src;
                h.station2 = oid;
                h.item = FUEL_CELLS;
                h.qty = want;
                h.policy = RoutePolicy::LowestRisk;
                h.insureCargo = pol.insure;
                setOrder(f, h);
                claimedStations.insert(oid);
                last = "Fuel run to " + o.name;
                done = true;
                break;
            }
        if (done) { busy++; continue; }
        // 1b. colonies running short: buy what they lack near the base and carry it out
        if (pol.supply)
            for (auto& col : colonies) {
                if (col.owner != c.id || stations[col.station].dead || col.sat > 0.85f || claimedStations.count(col.station)) continue;
                const Station& cs = stations[col.station];
                if (!routeOk(*this, c.id, baseSys, cs.system, pol)) continue;
                int worst = -1;
                double worstRatio = 0.9;
                for (auto& [it, perDay] : colonyNeeds(col)) {
                    double r = cs.stock[it] / std::max(1.0f, cs.target[it]);
                    if (r < worstRatio) { worstRatio = r; worst = it; }
                }
                if (worst < 0) continue;
                int64_t want = std::min<int64_t>((int64_t)(cs.target[worst] * 1.5 - cs.stock[worst]), (int64_t)(cap / item(worst).volume));
                Id src = NONE;
                Money px = 0;
                for (Id sy : area) {
                    for (Id st : systems[sy].stations) {
                        if (st == col.station || !stationUsable(st)) continue;
                        int64_t depth = 0;
                        Money ask = bestAsk(st, worst, &depth, c.id);
                        if (ask && depth >= want / 3) { src = st; px = ask; break; }
                    }
                    if (src != NONE) break;
                }
                if (src == NONE || want <= 0) continue;
                Money left = pol.budget > 0 ? divBudgetLeft(c.id, DIV_HAULAGE) : c.wallet / 4;
                int64_t buy = std::min<int64_t>(want - have(c.id, src, worst), left / std::max<Money>(1, px));
                Money spent = 0;
                if (buy > 0) instantBuy(c.id, src, worst, buy, px * 12 / 10, &spent);
                divBook(c.id, DIV_HAULAGE, 0, spent, true);
                if (have(c.id, src, worst) <= 0) continue;
                FleetOrder h;
                h.kind = OrderKind::Haul;
                h.station = src;
                h.station2 = col.station;
                h.item = worst;
                h.qty = want;
                h.colony = col.id;
                h.policy = RoutePolicy::LowestRisk;
                h.insureCargo = pol.insure;
                setOrder(f, h);
                claimedStations.insert(col.station);
                last = "Supplies for " + cs.name;
                done = true;
                break;
            }
        if (done) { busy++; continue; }
        // 2. materials for your projects, from the base to the yard
        if (pol.projects)
            for (auto& pj : projects) {
                if (pj.stage != ProjectStage::Funding || pj.founder != c.id || pj.yard == NONE || pj.yard == base || claimedStations.count(pj.yard)) continue;
                if (!stationUsable(pj.yard) || !routeOk(*this, c.id, baseSys, stations[pj.yard].system, pol)) continue;
                for (size_t i = 0; i < pj.need.size() && !done; i++) {
                    int64_t want = pj.need[i].second - pj.got[i] - have(c.id, pj.yard, pj.need[i].first);
                    int it = pj.need[i].first;
                    int64_t q = std::min<int64_t>({want, have(c.id, base, it), (int64_t)(cap / item(it).volume)});
                    if (q <= 0) continue;
                    FleetOrder h;
                    h.kind = OrderKind::Haul;
                    h.station = base;
                    h.station2 = pj.yard;
                    h.item = it;
                    h.qty = q;
                    h.project = pj.id;
                    h.policy = RoutePolicy::LowestRisk;
                    h.insureCargo = pol.insure;
                    setOrder(f, h);
                    claimedStations.insert(pj.yard);
                    last = "Materials for the " + pj.name;
                    done = true;
                }
                if (done) break;
            }
        if (done) { busy++; continue; }
        // 3. courier work from the board
        if (pol.couriers && f.docked != NONE)
            for (auto& ct : contracts) {
                if (ct.state != ContractState::Open || ct.kind != ContractKind::Courier || ct.expires <= now) continue;
                if (!inArea.count(stations[ct.from].system) || !inArea.count(stations[ct.to].system)) continue;
                if (!systemAllowed(*this, pol, stations[ct.to].system)) continue;
                if (!routeOk(*this, c.id, stations[ct.from].system, stations[ct.to].system, pol)) continue;
                if (ct.qty * item(ct.item).volume > cap || c.wallet < ct.collateral * 3) continue;
                if (!acceptContract(c.id, ct.id).ok) continue;
                FleetOrder h;
                h.kind = OrderKind::Haul;
                h.station = ct.from;
                h.station2 = ct.to;
                h.item = ct.item;
                h.qty = ct.qty;
                h.contract = ct.id;
                h.policy = RoutePolicy::LowestRisk;
                h.insureCargo = pol.insure;
                setOrder(f, h);
                last = "Courier contract to " + stations[ct.to].name;
                done = true;
                break;
            }
        if (done) { busy++; continue; }
        // 4. bring scattered stock home to the base
        if (pol.consolidate) {
            bool miningSells = c.divs[DIV_MINING].pol.automate && c.divs[DIV_MINING].pol.sell;
            Id bestSt = NONE;
            double bestV = 2000000.0 * CR;
            std::vector<std::pair<double, Id>> piles;
            for (Id sy : area)
                for (Id st : systems[sy].stations) {
                    if (st == base || !stationUsable(st) || claimedStations.count(st)) continue;
                    if (stations[st].isOutpost() && stations[st].owner == c.id) continue;
                    double v = 0;
                    auto& h = c.hangar;
                    for (auto it = h.lower_bound(hkey(st, 0)); it != h.end() && it->first < hkey(st + 1, 0); ++it) {
                        int i = (int)(it->first % HANGAR_KEY);
                        if (isOre(i) || isShipItem(i) || isBpo(i) || i >= BPC_FIRST || (miningSells && isMineral(i))) continue;
                        v += (double)refPrice(st, i) * it->second;
                    }
                    v /= 1.0 + 0.15 * hexDistance(systems[sy].hex, systems[baseSys].hex);
                    if (v > bestV) piles.push_back({v, st});
                }
            // the most valuable pile whose way home stays inside the limits
            std::sort(piles.begin(), piles.end(), [](auto& x, auto& y) { return x.first > y.first; });
            for (size_t k = 0; k < piles.size() && k < 6 && bestSt == NONE; k++)
                if (routeOk(*this, c.id, stations[piles[k].second].system, baseSys, pol)) bestSt = piles[k].second;
            if (bestSt != NONE) {
                FleetOrder h;
                h.kind = OrderKind::Haul;
                h.station = bestSt;
                h.station2 = base;
                h.policy = RoutePolicy::LowestRisk;
                h.insureCargo = pol.insure;
                setOrder(f, h);
                claimedStations.insert(bestSt);
                last = "Bringing stock home from " + stations[bestSt].name;
                busy++;
                continue;
            }
        }
        if (f.docked != base) {
            FleetOrder o;
            o.kind = OrderKind::Move;
            o.station = base;
            o.policy = RoutePolicy::LowestRisk;
            setOrder(f, o);
        }
    }
    ds.status = fleetsN == 0 ? "No fleets: assign haulers to the division"
                             : std::to_string(busy) + " of " + std::to_string(fleetsN) + " fleets working" + (last.empty() ? "" : "; " + last);
}

void World::divIndustry(Corp& c) {
    DivisionState& ds = c.divs[DIV_INDUSTRY];
    const DivPolicy& pol = ds.pol;
    const Person& dir = *director(c.id, DIV_INDUSTRY);
    Id H = divisionBase(c.id, DIV_INDUSTRY);
    if (!stationUsable(H)) { ds.status = "The base is not usable"; return; }
    int lines = std::min(pol.lines, 1 + dir.mgmt / 30);
    int busy = 0;
    bool researching = false;
    for (auto& j : jobs)
        if (j.active && j.corp == c.id && j.division == DIV_INDUSTRY) {
            if (j.kind == JobKind::Manufacture) busy++;
            else if (j.kind == JobKind::ResearchME || j.kind == JobKind::ResearchTE) researching = true;
        }
    // what our own blueprints here consume: those components stay for the next job
    std::map<int, int64_t> consumed;
    for (int b = 0; b < BP_OUTPOST; b++) {
        const BlueprintDef& d = blueprint(b);
        if (have(c.id, H, d.bpo) <= 0 && !(d.bpc >= 0 && have(c.id, H, d.bpc) > 0)) continue;
        if (pol.product >= 0 && b != pol.product) continue;
        for (auto& [m, q] : d.materials) consumed[m] += q * 2;
    }
    // list what was built, a little over cost
    if (pol.sell)
        for (int it = COMPONENT_FIRST; it < BPO_FIRST; it++) {
            int64_t n = have(c.id, H, it);
            if (consumed.count(it)) n -= consumed[it];
            if (n <= 0 || item(it).bp < 0) continue;
            bool built = pol.product >= 0 ? blueprint(pol.product).product == it : have(c.id, H, blueprint(item(it).bp).bpo) > 0;
            if (!built) continue;
            bool listed = false;
            if (const Book* bk = stations[H].findBook(it))
                for (auto& o : bk->sells) listed = listed || o.corp == c.id;
            if (listed) continue;
            const BlueprintDef& d = blueprint(item(it).bp);
            Money cost = 0;
            for (auto& [m, q] : d.materials) cost += refPrice(H, m) * matNeed(q, 1, c.me[item(it).bp]);
            cost /= std::max(1, d.perRun);
            Money ask = bestAsk(H, it, nullptr, c.id);
            Money px = std::max(cost * 110 / 100, ask ? ask * 98 / 100 : refPrice(H, it));
            placeOrder(c.id, H, it, false, px, n);
        }
    std::string doing;
    for (int line = busy; line < lines; line++) {
        int bestBp = -1;
        double bestMargin = 1.05;
        for (int bp = 0; bp < BP_OUTPOST; bp++) {
            if (pol.product >= 0 && bp != pol.product) continue;
            const BlueprintDef& d = blueprint(bp);
            bool hasBpo = have(c.id, H, d.bpo) > 0, hasBpc = d.bpc >= 0 && have(c.id, H, d.bpc) > 0;
            if (!hasBpo && !hasBpc) continue;
            bool busyBp = false;
            for (auto& j : jobs) busyBp = busyBp || (j.active && j.corp == c.id && j.item == bp && j.kind != JobKind::Refine && hasBpo && !hasBpc);
            if (busyBp) continue;
            int64_t stockQ = have(c.id, H, d.product);
            if (const Book* bk = stations[H].findBook(d.product))
                for (auto& o : bk->sells) stockQ += o.corp == c.id ? o.qty : 0;
            if (stockQ >= (isComponent(d.product) ? 3000 : isModule(d.product) ? 6 : 3)) continue;
            double cost = 0;
            for (auto& [m, q] : d.materials) {
                Money ask = bestAsk(H, m, nullptr, c.id);
                double have_ = (double)have(c.id, H, m);
                double need = (double)matNeed(q, 1, c.me[bp]);
                double unit = (double)(ask ? ask : refPrice(H, m) * 11 / 10);
                if (have_ >= need) unit = (double)refPrice(H, m) * 0.9;  // our own stock (from the mining division) is cheaper
                cost += unit * need;
            }
            double sale = (double)refPrice(H, d.product) * d.perRun;
            double margin = cost > 0 ? sale / cost * judgement(*this, dir) : 0;
            if (pol.product >= 0) margin = std::max(margin, 1.06);
            if (margin > bestMargin) { bestMargin = margin; bestBp = bp; }
        }
        if (bestBp < 0) break;
        const BlueprintDef& d = blueprint(bestBp);
        int64_t runs = isComponent(d.product) ? 2 : 1;
        bool ready = true;
        for (auto& [m, q] : d.materials) {
            int64_t need = matNeed(q, runs, c.me[bestBp]) - have(c.id, H, m);
            if (need <= 0) continue;
            Money left = pol.budget > 0 ? divBudgetLeft(c.id, DIV_INDUSTRY) : c.wallet / 3;
            Money px = refPrice(H, m) * 125 / 100;
            int64_t afford = std::min<int64_t>(need, left / std::max<Money>(1, px));
            Money spent = 0;
            if (afford > 0) instantBuy(c.id, H, m, afford, px, &spent);
            ds.spent += spent;  // against the budget; the cost is booked when the job uses it
            if (have(c.id, H, m) < matNeed(q, runs, c.me[bestBp])) ready = false;
        }
        if (!ready) { doing = "Short of materials for " + item(d.product).name; break; }
        Money before = c.wallet, matValue = 0;
        for (auto& [m, q] : d.materials) matValue += valueAt(H, m) * matNeed(q, runs, c.me[bestBp]);
        Result r = startManufacture(c.id, H, bestBp, runs);
        if (!r.ok) { doing = r.msg; break; }
        jobs.back().division = DIV_INDUSTRY;
        divBook(c.id, DIV_INDUSTRY, 0, matValue + (before - c.wallet));
        doing = "Building " + item(d.product).name;
        busy++;
    }
    // a free line and an original that could be better: research it
    if (pol.research && !researching && busy < lines) {
        int bp = pol.product >= 0 ? pol.product : -1;
        if (bp < 0)
            for (int b = 0; b < BP_OUTPOST; b++)
                if (have(c.id, H, blueprint(b).bpo) > 0 && (c.me[b] < 10 || c.te[b] < 10) && (bp < 0 || c.me[b] < c.me[bp])) bp = b;
        if (bp >= 0 && have(c.id, H, blueprint(bp).bpo) > 0) {
            JobKind k = c.me[bp] < 10 ? JobKind::ResearchME : c.te[bp] < 10 ? JobKind::ResearchTE : JobKind::Copy;
            if (k != JobKind::Copy) {
                Money before = c.wallet;
                bool affordable = pol.budget <= 0 || divBudgetLeft(c.id, DIV_INDUSTRY) > blueprint(bp).bpoPrice / 25;
                if (affordable && startResearch(c.id, H, bp, k, 1).ok) {
                    jobs.back().division = DIV_INDUSTRY;
                    divBook(c.id, DIV_INDUSTRY, 0, before - c.wallet, true);
                    if (doing.empty()) doing = "Researching " + item(blueprint(bp).product).name;
                }
            }
        }
    }
    ds.status = std::to_string(busy) + " of " + std::to_string(lines) + " lines busy at " + stations[H].name + (doing.empty() ? "" : "; " + doing);
}

void World::divSecurity(Corp& c) {
    DivisionState& ds = c.divs[DIV_SECURITY];
    const DivPolicy& pol = ds.pol;
    Id base = divisionBase(c.id, DIV_SECURITY);
    Id baseSys = stations[base].system;
    std::vector<int> bd;
    std::vector<Id> area = systemsWithin(baseSys, pol.radius, &bd);
    std::set<Id> inArea(area.begin(), area.end());
    // where we are needed, most urgent first
    std::vector<std::pair<double, Id>> needs;
    if (pol.guard)
        for (Id oid : outpostIds) {
            const Station& o = stations[oid];
            if (o.dead || o.owner != c.id) continue;
            bool besieged = o.besieger != NONE && now - o.lastSiege < 3 * MINUTES_PER_HOUR;
            if (besieged) needs.push_back({1e9, o.system});
        }
    if (pol.escort) {
        std::map<Id, double> value;
        for (Id fid : c.fleetList) {
            const Fleet& g = fleets[fid];
            if (!g.alive || g.division == DIV_SECURITY || fleetStrength(g) > 20) continue;
            Id sy = g.order.kind == OrderKind::Mine && g.order.system != NONE ? g.order.system : g.system;
            if (sy == NONE || !inArea.count(sy) || systems[sy].zone == Zone::Charter) continue;
            value[sy] += (double)fleetCargoValue(g) + 2e6 * CR;
        }
        for (auto& [sy, v] : value) needs.push_back({v * (1.0 + systems[sy].riskLive / 25.0), sy});
    }
    if (pol.bounties)
        for (auto& ct : contracts)
            if (ct.state == ContractState::Accepted && ct.contractor == c.id && ct.kind == ContractKind::Bounty)
                for (Id sy : area)
                    for (Id gid : fleetsIn[sy])
                        if (fleets[gid].alive && fleets[gid].corp == ct.target) needs.push_back({5e8, sy});
    std::sort(needs.begin(), needs.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::set<Id> covered;
    int fleetsN = 0, out = 0;
    for (Id fid : std::vector<Id>(c.fleetList)) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.division != DIV_SECURITY || f.battle != NONE) continue;
        fleetsN++;
        double hull = 0, n = 0;
        for (Id s : f.ships) { hull += ships[s].hull / std::max(1.0, stats(ships[s]).hull); n++; }
        if (n > 0 && hull / n < 0.5) {  // patched up at base first
            if (!(f.order.kind == OrderKind::Move && f.order.station == base)) {
                FleetOrder o;
                o.kind = OrderKind::Move;
                o.station = base;
                o.policy = RoutePolicy::LowestRisk;
                setOrder(f, o);
            }
            continue;
        }
        // go where we are needed and can win: never into a system whose hostile strength outguns us
        double mine = fleetStrength(f);
        auto threat = [&](Id sy) {
            double t = 0;
            for (Id gid : fleetsIn[sy]) {
                const Fleet& g = fleets[gid];
                if (g.alive && g.corp != c.id && hostileTo(g.corp, c.id)) t += fleetStrength(g);
            }
            return t + systems[sy].heat * 8.0;
        };
        Id target = baseSys;
        for (auto& [w_, sy] : needs)
            if (!covered.count(sy) && systems[sy].riskLive <= pol.maxRisk + 15 && threat(sy) < mine * 0.75) { target = sy; break; }
        covered.insert(target);
        if (f.order.kind == OrderKind::Patrol && f.order.system == target && f.phase != 99) { out++; continue; }
        FleetOrder o;
        o.kind = OrderKind::Patrol;
        o.system = target;
        o.policy = RoutePolicy::Shortest;
        setOrder(f, o);
        out++;
    }
    // take bounty work near the base
    if (pol.bounties && fleetsN > 0 && rng.chance(0.2))
        for (auto& ct : contracts)
            if (ct.state == ContractState::Open && ct.kind == ContractKind::Bounty && ct.expires > now && ct.target != c.id && hostileTo(c.id, ct.target)) {
                bool nearby = false;
                for (Id sy : area)
                    for (Id gid : fleetsIn[sy]) nearby = nearby || (fleets[gid].alive && fleets[gid].corp == ct.target);
                if (nearby && acceptContract(c.id, ct.id).ok) break;
            }
    ds.status = fleetsN == 0 ? "No fleets: assign warships to the division"
                             : std::to_string(out) + " fleets on patrol; " + std::to_string((int)needs.size()) + " places asking for cover";
}

// Wages charged to divisions, and the month's reports.
void World::divisionsDaily() {
    Corp& c = pc();
    for (int d = 0; d < DIV_COUNT; d++) {
        Money w = 0;
        if (const Person* p = director(c.id, d)) w += p->wage;
        for (Id fid : c.fleetList) {
            const Fleet& f = fleets[fid];
            if (!f.alive || f.division != d) continue;
            for (Id s : f.ships)
                if (ships[s].pilot != NONE) w += people[ships[s].pilot].wage;
        }
        c.divs[d].costs += w;
    }
    int day = (int)(now / MINUTES_PER_DAY);
    if (day > 0 && day % 30 == 0) divisionsMonthly();
    staffDaily();
    // smear campaigns fade
    for (auto& k : corps) k.smear = std::max(0.0f, k.smear * 0.96f - 0.002f);
    // rivals' spies: a few a month across the galaxy
    if (rng.chance(0.06)) {
        std::vector<Id> cand;
        for (auto& k : corps)
            if (k.alive && !k.player && (k.arch == Arch::Industrialist || k.arch == Arch::Trader)) cand.push_back(k.id);
        if (!cand.empty()) aiEspionage(corps[cand[rng.irange(0, (int)cand.size() - 1)]]);
    }
}

void World::divisionsMonthly() {
    Corp& c = pc();
    int month = (int)(now / MINUTES_PER_DAY / 30);
    for (int d = 0; d < DIV_COUNT; d++) {
        DivisionState& ds = c.divs[d];
        Person* p = director(c.id, d);
        int fleetsN = 0;
        Money fleetValue = 0;
        for (Id fid : c.fleetList)
            if (fleets[fid].alive && fleets[fid].division == d) {
                fleetsN++;
                for (Id s : fleets[fid].ships) fleetValue += shipValue(ships[s]);
            }
        bool used = ds.pol.automate || ds.revenue || ds.costs || fleetsN;
        if (used) {
            DivReport r;
            r.month = month;
            r.revenue = ds.revenue;
            r.costs = ds.costs;
            r.fleets = fleetsN;
            Money profit = ds.revenue - ds.costs;
            double scale = std::max(3e6 * CR, (double)fleetValue * 0.06 + (double)ds.pol.budget * 0.05);
            if (d == DIV_SECURITY || d == DIV_INTEL) r.score = (float)std::clamp(0.3 - (double)ds.costs / (scale * 4), -1.0, 1.0);
            else r.score = (float)std::clamp((double)profit / scale, -1.0, 1.0);
            if (!p) r.note = "No director";
            else if (r.score > 0.5) r.note = "An excellent month";
            else if (r.score > 0.1) r.note = "A good month";
            else if (r.score > -0.2) r.note = "A flat month";
            else r.note = "A poor month";
            ds.reports.push_back(r);
            if (ds.reports.size() > 12) ds.reports.erase(ds.reports.begin());
            if (ds.pol.automate)
                addLog(Chan::Corp, r.score >= 0 ? Sev::Info : Sev::Warn, true, NONE,
                       std::string(divisionName(d)) + " monthly report: revenue " + fmtMoneyShort(r.revenue) + ", costs " + fmtMoneyShort(r.costs) +
                           ", result " + fmtMoneyShort(profit) + ". " + r.note + ".");
            // the director's mood
            if (p && ds.pol.automate) {
                float dl = (float)(r.score * 5.0);
                Money fair = fairWage(*p);
                if (p->wage >= fair * 110 / 100) dl += 2;
                else if (p->wage < fair * 95 / 100) dl -= 3;
                if (p->trait == Trait::Steady) dl += 2;
                if (p->trait == Trait::Ambitious && p->wage < fair * 120 / 100) dl -= 2;
                p->loyalty = std::clamp(p->loyalty + dl, 0.0f, 100.0f);
                bool asking = false;
                for (auto& q : staffReqs) asking = asking || (q.open && q.person == p->id);
                if (!asking && r.score > 0.25 && rng.chance(p->ambition / 140.0)) {
                    StaffRequest q;
                    q.id = (Id)staffReqs.size();
                    q.kind = StaffReqKind::Raise;
                    q.person = p->id;
                    q.wage = p->wage * (Money)rng.irange(112, 130) / 100 / 1000 * 1000;
                    q.expires = now + 5 * MINUTES_PER_DAY;
                    staffReqs.push_back(q);
                    addLog(Chan::Corp, Sev::Info, true, NONE, p->name + " (" + seatName(divisionSeat(d)) + ") asks for a raise to " + fmtMoney(q.wage) + " a day after a good month.");
                }
            }
        }
        ds.revenue = ds.costs = ds.spent = 0;
    }
    // the CFO's mood follows the company's
    if (Id cfo = c.exec[SEAT_CFO]; cfo != NONE) {
        Person& p = people[cfo];
        p.loyalty = std::clamp(p.loyalty + (c.stats.size() > 30 && c.netWorth > c.stats[c.stats.size() - 30].netWorth ? 2.0f : -2.0f), 0.0f, 100.0f);
    }
}

}  // namespace nb
