// NebulaOne - industry jobs (refining, manufacturing, research, copying),
// loans, shares, staff and economic events.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

// ----------------------------------------------------------------- industry --
static bool industryOk(const World& w, Id station) {
    if (!w.stationUsable(station)) return false;
    const Station& st = w.stations[station];
    return !st.isOutpost() || st.state == OutpostState::Online;
}

Result World::startRefine(Id corp, Id station, int ore, int64_t qty) {
    if (!isOre(ore)) return Result::fail("Only ore can be refined.");
    if (!industryOk(*this, station)) return Result::fail("This station's refinery is not working.");
    int64_t batches = qty / 100;
    if (batches <= 0) return Result::fail("Refining works in batches of 100 units.");
    int64_t use = batches * 100;
    if (have(corp, station, ore) < use) return Result::fail("Not enough " + item(ore).name + " in this hangar.");
    double y = refineYield(corp, station);
    Job j;
    j.id = (Id)jobs.size();
    j.corp = corp;
    j.station = station;
    j.kind = JobKind::Refine;
    j.item = ore;
    j.runs = use;
    Money value = 0;
    for (int m = 0; m < MINERAL_COUNT; m++) {
        int64_t n = (int64_t)std::floor(item(ore).refine[m] * batches * y);
        if (n > 0) { j.out.push_back({MINERAL_FIRST + m, n}); value += item(MINERAL_FIRST + m).base * n; }
    }
    const Station& st = stations[station];
    bool outpost = st.isOutpost();
    Money fee = outpost ? (st.owner == corp ? 0 : (Money)(value * st.refineTax)) : value * 2 / 100;
    if (corps[corp].wallet < fee) return Result::fail("Not enough credits for the refining fee (" + fmtMoney(fee) + ").");
    takeItem(corp, station, ore, use);
    if (fee > 0) {
        pay(corp, -fee, Cat::RefineFee, "Refining " + fmtInt(use) + " " + item(ore).name);
        if (outpost && st.owner != NONE) {
            pay(st.owner, fee, Cat::Taxes, "Refinery tax at " + st.name);
            stations[station].taxCollected += fee;
        }
    }
    j.start = now;
    j.end = now + (Tick)std::max(10.0, 60.0 * (1.0 - 0.2 * execBonus(corp, SEAT_INDUSTRY)) * (1.0 - 0.15 * smodBonus(station, SMOD_REFINERY)));
    jobs.push_back(j);
    if (corp == player) pstats.refines++;
    return Result::good("Refining " + fmtInt(use) + " " + item(ore).name + " at " + std::to_string((int)std::round(y * 100)) + "% yield.", use, fee);
}

static int64_t blueprintsBusy(const World& w, Id corp, Id station, int bp) {
    int64_t busy = 0;
    for (auto& j : w.jobs)
        if (j.active && j.corp == corp && j.station == station && j.item == bp && j.kind != JobKind::Refine && !j.usedCopies) busy++;
    return busy;
}

int64_t matNeed(int64_t perRun, int64_t runs, int me) {
    double f = 1.0 - 0.01 * std::clamp(me, 0, 10);
    return std::max<int64_t>(runs, (int64_t)std::ceil(perRun * runs * f));
}

Result World::startManufacture(Id corp, Id station, int bp, int64_t runs) {
    if (bp < 0 || bp >= BLUEPRINT_COUNT) return Result::fail("Unknown blueprint.");
    if (runs <= 0) return Result::fail("Choose at least one run.");
    if (!industryOk(*this, station)) return Result::fail("This station's factory is not working.");
    const BlueprintDef& d = blueprint(bp);
    const std::string& pname = item(d.product).name;
    int64_t bpos = have(corp, station, d.bpo) - blueprintsBusy(*this, corp, station, bp);
    int64_t copies = d.bpc >= 0 ? have(corp, station, d.bpc) : 0;
    bool useCopies = false;
    if (bpos <= 0) {
        if (copies >= runs) useCopies = true;
        else if (copies > 0) return Result::fail("You have " + fmtInt(copies) + " copies here; that is " + fmtInt(copies) + " runs at most.");
        else return Result::fail("You need the " + pname + " Blueprint (free) or copies of it in this station's hangar.");
    }
    Corp& c = corps[corp];
    int me = c.me[bp], te = c.te[bp];
    for (auto& [m, q] : d.materials) {
        int64_t need = matNeed(q, runs, me);
        if (have(corp, station, m) < need) return Result::fail("Missing " + fmtInt(need - have(corp, station, m)) + " " + item(m).name + " here.");
    }
    Money fee = d.materialBase() * runs / 100;
    if (c.wallet < fee) return Result::fail("Not enough credits for the install fee (" + fmtMoney(fee) + ").");
    for (auto& [m, q] : d.materials) takeItem(corp, station, m, matNeed(q, runs, me));
    if (useCopies) takeItem(corp, station, d.bpc, runs);
    pay(corp, -fee, Cat::ManufactureFee, "Manufacturing " + std::to_string(runs) + " x " + pname);
    Job j;
    j.id = (Id)jobs.size();
    j.corp = corp;
    j.station = station;
    j.kind = JobKind::Manufacture;
    j.item = bp;
    j.runs = runs;
    j.usedCopies = useCopies;
    j.start = now;
    double mins = d.minutes * (double)runs * (1.0 - 0.02 * te) * (1.0 - 0.2 * execBonus(corp, SEAT_INDUSTRY)) *
                  (1.0 - 0.10 * smodBonus(station, SMOD_FACTORY));
    if (stations[station].type == StationType::Industrial) mins *= 0.85;
    j.end = now + std::max<Tick>(5, (Tick)mins);
    j.out.push_back({d.product, d.perRun * runs});
    jobs.push_back(j);
    return Result::good("Building " + fmtInt(d.perRun * runs) + " x " + pname + ", ready in " + fmtDuration(j.end - now) + ".", runs, fee);
}

Tick researchMinutes(const BlueprintDef& d, int level) { return (Tick)(std::max(60, d.minutes * 2) * (level + 1)); }

Result World::startResearch(Id corp, Id station, int bp, JobKind kind, int64_t runs) {
    if (bp < 0 || bp >= BLUEPRINT_COUNT) return Result::fail("Unknown blueprint.");
    if (runs <= 0) return Result::fail("Choose at least one level or copy.");
    if (!industryOk(*this, station)) return Result::fail("This station's laboratories are not working.");
    const BlueprintDef& d = blueprint(bp);
    const std::string& pname = item(d.product).name;
    if (have(corp, station, d.bpo) - blueprintsBusy(*this, corp, station, bp) <= 0)
        return Result::fail("You need a free " + pname + " Blueprint original in this station's hangar.");
    Corp& c = corps[corp];
    Job j;
    j.id = (Id)jobs.size();
    j.corp = corp;
    j.station = station;
    j.kind = kind;
    j.item = bp;
    j.runs = runs;
    j.start = now;
    double mins = 0;
    Money fee = 0;
    double lab = stations[station].type == StationType::Research ? 0.7 : 1.0;
    if (kind == JobKind::ResearchME || kind == JobKind::ResearchTE) {
        int lvl = kind == JobKind::ResearchME ? c.me[bp] : c.te[bp];
        for (auto& o : jobs)
            if (o.active && o.corp == corp && o.item == bp && o.kind == kind) lvl += (int)o.runs;
        if (lvl + runs > 10) return Result::fail("Research tops out at level 10.");
        for (int k = 0; k < runs; k++) mins += researchMinutes(d, lvl + k);
        fee = d.bpoPrice * 2 / 100 * runs;
    } else if (kind == JobKind::Copy) {
        if (d.bpc < 0) return Result::fail("Only ship blueprints can be copied.");
        mins = d.minutes * 0.5 * runs;
        fee = d.bpoPrice / 200 * runs;
        j.out.push_back({d.bpc, runs});
    } else {
        return Result::fail("Unknown research job.");
    }
    mins *= lab * (1.0 - 0.2 * execBonus(corp, SEAT_INDUSTRY)) * (1.0 - 0.20 * smodBonus(station, SMOD_LAB));
    if (c.wallet < fee) return Result::fail("The laboratory fee is " + fmtMoney(fee) + ".");
    pay(corp, -fee, Cat::ResearchFee, std::string(jobKindName(kind)) + ": " + pname);
    j.end = now + std::max<Tick>(10, (Tick)mins);
    jobs.push_back(j);
    return Result::good(std::string(jobKindName(kind)) + " on the " + pname + " Blueprint, done in " + fmtDuration(j.end - now) + ".", runs, fee);
}

void World::jobsTick() {
    for (auto& j : jobs) {
        if (!j.active || j.end > now) continue;
        j.active = false;
        for (auto& [it, q] : j.out) addItem(j.corp, j.station, it, q);
        Corp& c = corps[j.corp];
        if (j.division >= 0 && c.player && j.kind == JobKind::Manufacture) {
            Money v = 0;
            for (auto& [it, q] : j.out) v += valueAt(j.station, it) * q;
            divBook(j.corp, j.division, v, 0);
        }
        if (j.kind == JobKind::ResearchME) c.me[j.item] = (uint8_t)std::min<int64_t>(10, c.me[j.item] + j.runs);
        if (j.kind == JobKind::ResearchTE) c.te[j.item] = (uint8_t)std::min<int64_t>(10, c.te[j.item] + j.runs);
        if (j.corp == player) {
            std::string what;
            switch (j.kind) {
                case JobKind::Refine: what = "refined " + item(j.item).name; break;
                case JobKind::Manufacture:
                    pstats.manufactured += j.runs;
                    what = fmtInt(j.out.empty() ? j.runs : j.out[0].second) + " x " + item(blueprint(j.item).product).name;
                    break;
                case JobKind::ResearchME: what = item(blueprint(j.item).product).name + " material efficiency now " + std::to_string(c.me[j.item]); break;
                case JobKind::ResearchTE: what = item(blueprint(j.item).product).name + " time efficiency now " + std::to_string(c.te[j.item]); break;
                case JobKind::Copy: what = fmtInt(j.runs) + " " + item(blueprint(j.item).product).name + " copies"; break;
            }
            addLog(Chan::Corp, Sev::Good, true, stations[j.station].system, "Job complete at " + stations[j.station].name + ": " + what);
        }
    }
    if (jobs.size() > 400 && now % MINUTES_PER_DAY == 0) {
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const Job& j) { return !j.active; }), jobs.end());
        for (size_t i = 0; i < jobs.size(); i++) jobs[i].id = (Id)i;
    }
}

// ------------------------------------------------------------------ finance --
static const double RATE_BY_RATING[5] = {0.015, 0.022, 0.030, 0.038, 0.045};

Money World::bankCreditLimit(Id corp) const {
    Money lim = std::max<Money>(1000000 * CR, corps[corp].netWorth / 2) - loansOwed(corp);
    if (corps[corp].rating >= 4) lim /= 2;
    return std::max<Money>(0, lim);
}
double World::bankRate(Id corp) const { return RATE_BY_RATING[std::clamp(corps[corp].rating, 0, 4)]; }

static Loan makeLoan(World& w, Id lender, Id borrower, Money amount, double rate30, int days) {
    Loan l;
    l.id = (Id)w.loans.size();
    l.lender = lender;
    l.borrower = borrower;
    l.principal = amount;
    l.rate30 = rate30;
    l.days = days;
    l.remaining = (Money)(amount * (1.0 + rate30 * days / 30.0));
    l.daily = l.remaining / days + 1;
    l.start = w.now;
    return l;
}

Result World::takeBankLoan(Id corp, Money amount, int days) {
    if (amount <= 0) return Result::fail("Enter an amount.");
    if (days != 30 && days != 60 && days != 90) return Result::fail("Terms are 30, 60 or 90 days.");
    corps[corp].netWorth = computeNetWorth(corp);
    if (amount > bankCreditLimit(corp)) return Result::fail("That is above your credit limit of " + fmtMoney(bankCreditLimit(corp)) + ".");
    Loan l = makeLoan(*this, NONE, corp, amount, bankRate(corp), days);
    loans.push_back(l);
    pay(corp, amount, Cat::LoanIn, "Consolidated Bank loan, " + std::to_string(days) + " days");
    if (corp == player)
        addLog(Chan::Corp, Sev::Info, true, NONE, "Borrowed " + fmtMoney(amount) + " from the Consolidated Bank. Daily payment " + fmtMoney(l.daily) + ".");
    return Result::good("Loan approved.", 0, amount);
}

Result World::fundRequest(Id corp, Id reqId) {
    if (reqId < 0 || reqId >= (Id)loanReqs.size()) return Result::fail("No such request.");
    LoanRequest& r = loanReqs[reqId];
    if (!r.open || r.expires <= now) return Result::fail("That request is no longer open.");
    if (r.corp == corp) return Result::fail("You cannot fund your own request.");
    if (corps[corp].wallet < r.amount) return Result::fail("Not enough credits.");
    Loan l = makeLoan(*this, corp, r.corp, r.amount, r.rate30, r.days);
    loans.push_back(l);
    pay(corp, -r.amount, Cat::LendOut, "Loan to " + corps[r.corp].name);
    pay(r.corp, r.amount, Cat::LoanIn, "Loan from " + corps[corp].name);
    r.open = false;
    if (corp == player)
        addLog(Chan::Corp, Sev::Info, true, NONE, "You lent " + fmtMoney(r.amount) + " to " + corps[r.corp].name + " at " +
                                                      std::to_string(r.rate30 * 100).substr(0, 4) + "% per 30 days.");
    return Result::good("Loan funded.", 0, r.amount);
}

static int64_t sharesHeldByCorps(const World& w, Id target) {
    int64_t held = 0;
    for (auto& c : w.corps) {
        auto it = c.shares.find(target);
        if (it != c.shares.end()) held += it->second;
    }
    return held;
}

Result World::tradeShares(Id corp, Id target, int64_t qty, bool buy) {
    if (target < 0 || target >= (Id)corps.size() || !corps[target].alive) return Result::fail("That corporation is not listed.");
    Corp& t = corps[target];
    if (t.player && !t.listed) return Result::fail(t.name + " is privately held.");
    if (target == corp) return Result::fail("Use a buyback to buy your own shares.");
    if (qty <= 0) return Result::fail("Enter a number of shares.");
    double impact = 2.0 * (double)qty / (double)t.sharesOut;
    Money px = buy ? (Money)(t.sharePrice * (1.0 + impact / 2)) : (Money)(t.sharePrice * (1.0 - impact / 2));
    px = std::max<Money>(1, px);
    Money gross = px * qty, fee = gross / 200;
    // the float: what outside investors hold. For AI corporations every share not held by a corporation.
    int64_t floatLeft = t.player ? t.publicFloat - sharesHeldByCorps(*this, target) : t.sharesOut - sharesHeldByCorps(*this, target);
    if (buy) {
        if (qty > floatLeft) return Result::fail("Only " + fmtInt(std::max<int64_t>(0, floatLeft)) + " shares are available to buy.");
        if (corps[corp].wallet < gross + fee) return Result::fail("Not enough credits.");
        pay(corp, -gross - fee, Cat::ShareTrade, "Bought " + fmtInt(qty) + " " + t.ticker + " @ " + fmtMoney(px, false));
        corps[corp].shares[target] += qty;
        t.sharePrice = (Money)(t.sharePrice * (1.0 + impact));
        // an armed poison pill: the player raiding an AI corporation, or a raider on the listed player
        bool raid = t.player ? corp == t.raider : corps[corp].player;
        if (t.poisonPill && raid && corps[corp].shares[target] * 4 > t.sharesOut) {
            firePill(target, corp);
            if (t.player) {
                addLog(Chan::Corp, Sev::Good, true, NONE, corps[corp].name + " is diluted by your poison pill and backs off.");
                t.raider = NONE;
                t.raidDeadline = 0;
            }
            return Result::good("Bought " + fmtInt(qty) + " " + t.ticker + ", but the poison pill fired: your stake is now " +
                                    fmtInt(corps[corp].shares[target]) + " of " + fmtInt(t.sharesOut) + " shares.",
                                qty, gross);
        }
    } else {
        auto it = corps[corp].shares.find(target);
        if (it == corps[corp].shares.end() || it->second < qty) return Result::fail("You do not hold that many shares.");
        it->second -= qty;
        if (it->second == 0) corps[corp].shares.erase(it);
        pay(corp, gross - fee, Cat::ShareTrade, "Sold " + fmtInt(qty) + " " + t.ticker + " @ " + fmtMoney(px, false));
        t.sharePrice = std::max<Money>(1, (Money)(t.sharePrice * (1.0 - impact)));
    }
    return Result::good(std::string(buy ? "Bought " : "Sold ") + fmtInt(qty) + " " + t.ticker + " at " + fmtMoney(px), qty, gross);
}

void World::financeDaily() {
    for (auto& l : loans) {
        if (l.status != 0) continue;
        Corp& b = corps[l.borrower];
        Money due = std::min(l.daily, l.remaining);
        double interestShare = 1.0 - 1.0 / (1.0 + l.rate30 * l.days / 30.0);
        if (b.wallet >= due) {
            Money interest = (Money)(due * interestShare);
            pay(l.borrower, -(due - interest), Cat::LoanRepay, "Loan installment (principal)");
            pay(l.borrower, -interest, Cat::LoanInterest, "Loan installment (interest)");
            if (l.lender != NONE) {
                pay(l.lender, due - interest, Cat::LendRepaid, "Repayment from " + b.name);
                pay(l.lender, interest, Cat::LoanInterest, "Interest from " + b.name);
            }
            l.remaining -= due;
            l.missed = 0;
            if (l.remaining <= 0) {
                l.status = 1;
                if (l.lender != NONE) addStanding(l.lender, l.borrower, 1.0f);
                if (l.borrower == player || l.lender == player)
                    addLog(Chan::Corp, Sev::Good, true, NONE,
                           "Loan between " + (l.lender == NONE ? std::string("the Consolidated Bank") : corps[l.lender].name) + " and " +
                               b.name + " repaid in full.");
            }
        } else {
            l.missed++;
            l.remaining += l.remaining / 100;
            if (l.borrower == player) {
                b.rating = std::min(4, b.rating + (l.missed == 1 ? 1 : 0));
                addLog(Chan::Corp, Sev::Bad, true, NONE, "Missed a loan installment of " + fmtMoney(due) + ". Penalty interest added.");
            }
            int limit = l.lender == NONE ? 21 : 7;
            if (l.missed >= limit) {
                l.status = 2;
                Money rec = l.remaining * 3 / 10;
                l.recovered = rec;
                pay(l.borrower, -rec, Cat::LoanRepay, "Collateral seized after default");
                if (l.lender != NONE) {
                    pay(l.lender, rec, Cat::LendRepaid, "Recovered from defaulted loan to " + b.name);
                    addStanding(l.lender, l.borrower, -4.0f);
                }
                b.rating = 4;
                if (l.borrower == player || l.lender == player)
                    addLog(Chan::Corp, Sev::Bad, true, NONE, b.name + " defaulted on a loan of " + fmtMoney(l.principal) + ". Recovered " + fmtMoney(rec) + ".");
            }
        }
    }
    if (loans.size() > 2000) {
        // settled loans older than 90 days are only history
        loans.erase(std::remove_if(loans.begin(), loans.end(), [&](const Loan& l) { return l.status != 0 && now - l.start > 90 * MINUTES_PER_DAY; }),
                    loans.end());
        for (size_t i = 0; i < loans.size(); i++) loans[i].id = (Id)i;
    }
    for (auto& s : ships)
        if (s.alive && s.insured && s.insuredUntil <= now) {
            s.insured = false;
            if (corps[s.corp].player)
                addLog(Chan::Corp, Sev::Warn, true, NONE, "Hull insurance expired on " + (s.name.empty() ? shipDef(s.def).name : s.name) + ".");
        }
    // AI fleet upkeep (stands in for their crews' wages)
    for (auto& c : corps) {
        if (c.player || !c.alive) continue;
        int n = 0;
        for (Id s : c.shipList) n += ships[s].alive && ships[s].corp == c.id;
        pay(c.id, -(Money)n * 2500 * CR, Cat::Upkeep, "Crews and upkeep");
    }
    {
        Corp& p = pc();
        int missedRecently = 0;
        for (auto& l : loans)
            if (l.borrower == player && (l.missed > 0 || (l.status == 2 && now - l.start < 60 * MINUTES_PER_DAY))) missedRecently++;
        int r = p.netWorth > 250000000 * CR ? 0 : p.netWorth > 50000000 * CR ? 1 : p.netWorth > 1000000 * CR ? 2 : 3;
        p.rating = std::clamp(r + missedRecently, 0, 4);
    }
    for (auto& r : loanReqs)
        if (r.open && r.expires <= now) {
            r.open = false;
            if (rng.chance(0.5)) {
                Loan l = makeLoan(*this, NONE, r.corp, r.amount, r.rate30 * 0.9, r.days);
                loans.push_back(l);
                pay(r.corp, r.amount, Cat::LoanIn, "Bank loan");
            }
        }
    std::vector<int> openReq(corps.size(), 0), activeLoans(corps.size(), 0);
    for (auto& r : loanReqs) openReq[r.corp] += r.open;
    for (auto& l : loans) activeLoans[l.borrower] += l.status == 0;
    for (auto& c : corps) {
        if (c.player || !c.alive) continue;
        if (openReq[c.id] || activeLoans[c.id] >= 2 || !rng.chance(0.10)) continue;
        LoanRequest r;
        r.id = (Id)loanReqs.size();
        r.corp = c.id;
        double amt = std::clamp(toCr(c.netWorth) * rng.range(0.04, 0.18), 2e6, 60e6);
        r.amount = credits(std::round(amt / 250000.0) * 250000.0);
        r.rating = c.rating;
        const double base[5] = {0.016, 0.022, 0.030, 0.042, 0.055};
        r.rate30 = std::round((base[std::clamp(c.rating, 0, 4)] + rng.range(-0.002, 0.004)) * 1000.0) / 1000.0;
        const int terms[4] = {15, 30, 45, 60};
        r.days = terms[rng.irange(0, 3)];
        r.expires = now + 3 * MINUTES_PER_DAY;
        loanReqs.push_back(r);
    }
    if (loanReqs.size() > 300) {
        loanReqs.erase(std::remove_if(loanReqs.begin(), loanReqs.end(), [](const LoanRequest& r) { return !r.open; }), loanReqs.end());
        for (size_t i = 0; i < loanReqs.size(); i++) loanReqs[i].id = (Id)i;
    }
}

void World::sharesDaily() {
    int day = (int)(now / MINUTES_PER_DAY);
    for (auto& c : corps) {
        if (!c.alive) continue;
        double nw = std::max(1.0, (double)c.netWorth);
        if (c.player && !c.listed) {  // private: the book value per share
            c.sharePrice = std::max<Money>(1, (Money)(nw / c.sharesOut));
            c.shareHist.push_back(c.sharePrice);
            if (c.shareHist.size() > 400) c.shareHist.erase(c.shareHist.begin());
            continue;
        }
        double past = c.stats.size() > 30 ? (double)c.stats[c.stats.size() - 31].netWorth : (double)c.stats.front().netWorth;
        double r = (nw - past) / nw;
        double sentiment = std::clamp(1.0 + 2.0 * r, 0.5, 1.8);
        if (c.player) sentiment *= 0.8 + 0.4 * c.board / 100.0;
        sentiment *= 1.0 - c.smear;  // a smear campaign weighs on the price until it fades
        double target = nw / (double)c.sharesOut * sentiment;
        double p = (double)c.sharePrice;
        p += (target - p) * 0.12;
        p *= std::exp(rng.normal(0, 0.015));
        c.sharePrice = std::max<Money>(1, (Money)p);
        c.shareHist.push_back(c.sharePrice);
        if (c.shareHist.size() > 400) c.shareHist.erase(c.shareHist.begin());
        if (!c.player && day % 30 == 0) {
            Money growth = c.netWorth - c.profitPeriod;
            Money div = std::min(growth / 5, std::max<Money>(0, c.wallet / 4));
            if (div > 0) {
                Money perShare = div / c.sharesOut;
                for (auto& h : corps) {
                    auto it = h.shares.find(c.id);
                    if (it == h.shares.end() || it->second <= 0 || perShare <= 0) continue;
                    Money amt = perShare * it->second;
                    pay(h.id, amt, Cat::Dividends, "Dividend from " + c.name);
                    if (h.player) addLog(Chan::Corp, Sev::Good, true, NONE, c.name + " paid you a dividend of " + fmtMoney(amt) + ".");
                }
                for (auto& s : shorts)  // short sellers owe the dividend to the lender
                    if (s.open && s.target == c.id && perShare > 0)
                        pay(s.corp, -perShare * s.qty, Cat::Dividends, "Dividend owed on short " + c.ticker);
                pay(c.id, -div, Cat::Dividends, "Dividend to shareholders");
                bool heldByPlayer = pc().shares.count(c.id) > 0;
                if (heldByPlayer || corps.size() < 30)
                    addLog(Chan::News, Sev::Info, heldByPlayer, NONE, c.name + " declares a dividend of " + fmtMoney(perShare) + " per share.");
            }
            c.profitPeriod = c.netWorth;
        }
    }
}

// ------------------------------------------------------------------- people --
void World::peopleDaily() {
    int day = (int)(now / MINUTES_PER_DAY);
    Money wages = 0;
    for (auto& p : people)
        if (p.alive && p.employer == player) wages += p.wage;
    if (wages) pay(player, -wages, Cat::Wages, "Daily wages");
    for (auto& p : people)
        if (p.alive && p.injuredUntil && p.injuredUntil <= now && p.employer == player) {
            p.injuredUntil = 0;
            addLog(Chan::Corp, Sev::Info, true, NONE, p.name + " is fit for duty again.");
        }
    if (day % 7 == 0) {
        for (auto& p : people)
            if (p.employer == NONE && p.hub != NONE && p.availableUntil <= now) { p.hub = NONE; p.alive = false; }
        for (Id h : recruitHubs()) genPeople(h, 6, 2);
        addLog(Chan::News, Sev::Info, false, NONE, "New candidates are looking for work at the trade hubs near you.");
    }
    for (auto& c : corps) {
        if (c.player || !c.alive) continue;
        int hulls = 0;
        for (Id s : c.shipList) hulls += ships[s].alive && ships[s].corp == c.id;
        if (c.wallet < 0 || (hulls == 0 && c.wallet < 5000000 * CR)) c.brokeDays++; else c.brokeDays = 0;
        if (c.brokeDays >= 6) {
            c.wallet = std::max(c.wallet, (Money)0) + 20000000 * CR;
            c.brokeDays = 0;
            c.sharePrice = std::max<Money>(1, c.sharePrice / 2);
            c.rating = 4;
            addLog(Chan::News, Sev::Warn, pc().shares.count(c.id) > 0, NONE,
                   c.name + " restructures after heavy losses; new investors inject capital and the share price halves.");
        }
    }
}

}  // namespace nb
