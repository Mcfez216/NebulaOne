// NebulaOne - the corporate game: going public, the board, dividends,
// buybacks and takeovers of AI corporations through the share market.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

Money World::ipoValuation() const { return pc().netWorth * 12 / 10; }

Result World::goPublic(Id corp, double fraction) {
    Corp& c = corps[corp];
    if (!c.player) return Result::fail("Only your own corporation can be floated here.");
    if (c.listed) return Result::fail(c.name + " is already listed.");
    c.netWorth = computeNetWorth(corp);
    if (c.netWorth < 50000000 * CR) return Result::fail("The Exchange lists corporations worth 50M cr or more. You are worth " + fmtMoneyShort(c.netWorth) + ".");
    if (now - c.founded < 30 * MINUTES_PER_DAY) return Result::fail("The Exchange needs 30 days of accounts first.");
    if (fraction < 0.10 || fraction > 0.49) return Result::fail("Float between 10% and 49% of the company; you keep control.");
    Money valuation = ipoValuation();
    Money price = std::max<Money>(1, valuation / c.sharesOut);
    int64_t sold = (int64_t)(c.sharesOut * fraction);
    Money proceeds = price * sold * 95 / 100;
    pay(corp, proceeds, Cat::Equity, "Initial public offering: " + fmtInt(sold) + " shares at " + fmtMoney(price));
    c.listed = true;
    c.publicFloat = sold;
    c.sharePrice = price;
    c.board = 60;
    c.boardTarget = 0.08f;
    c.netWorth = computeNetWorth(corp);
    c.reviewBase = c.netWorth / c.sharesOut;
    c.nextReview = now + 30 * MINUTES_PER_DAY;
    addLog(Chan::News, Sev::Good, true, NONE,
           c.name + " (" + c.ticker + ") lists on the Exchange, raising " + fmtMoneyShort(proceeds) + ". The board will review results every 30 days.");
    return Result::good("Listed. You raised " + fmtMoney(proceeds) + " after the 5% underwriting fee.", sold, proceeds);
}

static Result declareDividend(World& w, Id corp, Money total) {
    Corp& c = w.corps[corp];
    if (!c.listed) return Result::fail("Only listed corporations pay dividends.");
    if (total <= 0) return Result::fail("Enter an amount.");
    if (c.wallet < total) return Result::fail("Not enough credits.");
    Money perShare = total / c.sharesOut;
    if (perShare <= 0) return Result::fail("That is less than a hundredth of a credit per share.");
    // only the float is paid out: your own holding is the company's
    Money paid = 0;
    int64_t corpHeld = 0;
    for (auto& h : w.corps) {
        auto it = h.shares.find(corp);
        if (it == h.shares.end() || it->second <= 0) continue;
        Money amt = perShare * it->second;
        w.pay(h.id, amt, Cat::Dividends, "Dividend from " + c.name);
        paid += amt;
        corpHeld += it->second;
    }
    Money publicPart = perShare * std::max<int64_t>(0, c.publicFloat - corpHeld);
    paid += publicPart;
    w.pay(corp, -paid, Cat::Dividends, "Dividend of " + fmtMoney(perShare) + " per share");
    double yield = (double)paid / std::max<double>(1.0, (double)c.netWorth);
    c.board = std::min(100.0f, c.board + (float)std::min(15.0, yield * 300.0));
    w.addLog(Chan::News, Sev::Info, true, NONE, c.name + " declares a dividend of " + fmtMoney(perShare) + " per share.");
    return Result::good("Paid " + fmtMoney(paid) + " to outside shareholders. The board approves.", 0, paid);
}

static Result buyback(World& w, Id corp, int64_t qty) {
    Corp& c = w.corps[corp];
    if (!c.listed) return Result::fail("Only listed corporations can buy back shares.");
    int64_t corpHeld = 0;
    for (auto& h : w.corps) {
        auto it = h.shares.find(corp);
        if (it != h.shares.end()) corpHeld += it->second;
    }
    int64_t avail = c.publicFloat - corpHeld;
    if (qty <= 0 || qty > avail) return Result::fail("Between 1 and " + fmtInt(std::max<int64_t>(0, avail)) + " shares are with the public.");
    double impact = 2.0 * (double)qty / (double)c.sharesOut;
    Money px = (Money)(c.sharePrice * (1.0 + impact / 2) * 1.05);
    Money cost = px * qty;
    if (c.wallet < cost) return Result::fail("That costs " + fmtMoney(cost) + ".");
    w.pay(corp, -cost, Cat::Equity, "Share buyback: " + fmtInt(qty) + " at " + fmtMoney(px));
    c.publicFloat -= qty;
    c.sharePrice = (Money)(c.sharePrice * (1.0 + impact));
    c.board = std::min(100.0f, c.board + 3.0f);
    if (c.publicFloat <= corpHeld) {
        if (corpHeld == 0) {
            c.listed = false;
            w.addLog(Chan::News, Sev::Info, true, NONE, c.name + " has bought back its last public shares and leaves the Exchange.");
            return Result::good("Bought back the last of the float: " + c.name + " is private again.", qty, cost);
        }
    }
    return Result::good("Bought back " + fmtInt(qty) + " shares for " + fmtMoney(cost) + ".", qty, cost);
}

Result World::merge(Id corp, Id target) {
    if (target < 0 || target >= (Id)corps.size() || target == corp) return Result::fail("Pick a corporation.");
    Corp& me = corps[corp];
    Corp& t = corps[target];
    if (!t.alive || t.player) return Result::fail("That corporation cannot be acquired.");
    int64_t held = me.shares.count(target) ? me.shares[target] : 0;
    if (held * 2 <= t.sharesOut) return Result::fail("You need more than half of " + t.ticker + "'s shares (you hold " + fmtInt(held) + " of " + fmtInt(t.sharesOut) + ").");
    int64_t minority = t.sharesOut - held;
    Money cost = minority * t.sharePrice;
    if (me.wallet < cost) return Result::fail("Buying out the minority shareholders costs " + fmtMoney(cost) + ".");
    pay(corp, -cost, Cat::Equity, "Takeover of " + t.name + ": minority buyout");
    // cancel the target's market orders (escrow returns to it first)
    std::vector<int64_t> ids;
    for (int64_t spot : t.orderSpots) {
        Book* bk = stations[(Id)(spot / HANGAR_KEY)].findBook((int)(spot % HANGAR_KEY));
        if (!bk) continue;
        for (auto& o : bk->sells) if (o.corp == target) ids.push_back(o.id);
        for (auto& o : bk->buys) if (o.corp == target) ids.push_back(o.id);
    }
    for (auto id : ids) cancelOrder(id, target);
    // everything it owns becomes yours
    pay(corp, t.wallet, Cat::Equity, "Cash acquired with " + t.name);
    t.wallet = 0;
    for (auto& [k, q] : t.hangar) me.hangar[k] += q;
    t.hangar.clear();
    for (auto& s : ships)
        if (s.alive && s.corp == target) s.corp = corp;
    for (auto& f : fleets)
        if (f.alive && f.corp == target) {
            f.corp = corp;
            f.name = me.ticker + " " + f.name;
            FleetOrder o;
            o.kind = f.docked != NONE ? OrderKind::None : OrderKind::Move;
            o.station = f.docked != NONE ? f.docked : nearestStation(f.system);
            if (o.station == NONE) o.kind = OrderKind::None;
            setOrder(f, o);
        }
    for (auto& l : loans) {
        if (l.status != 0) continue;
        if (l.borrower == target) l.borrower = corp;
        if (l.lender == target) l.lender = corp;
    }
    for (auto& r : loanReqs)
        if (r.corp == target) r.open = false;
    for (auto& j : jobs)
        if (j.active && j.corp == target) j.corp = corp;
    for (Id sid : outpostIds)
        if (!stations[sid].dead && stations[sid].owner == target) {
            stations[sid].owner = corp;
            systems[stations[sid].system].owner = corp;
        }
    for (auto& [cid, n] : t.shares)
        if (cid != corp) me.shares[cid] += n;
    for (auto& b : bonds) {  // its bond holdings come too; its own bonds become yours to pay
        if (b.done) continue;
        auto it = b.held.find(target);
        if (it != b.held.end()) { b.held[corp] += it->second; b.held.erase(target); }
        if (b.issuer == target) b.issuer = corp;
        b.onOffer = b.issuer == corp ? 0 : b.onOffer;
    }
    if (me.player && me.raider == target) { me.raider = NONE; me.raidDeadline = 0; }
    t.shares.clear();
    for (int b = 0; b < BLUEPRINT_COUNT; b++) {
        me.me[b] = std::max(me.me[b], t.me[b]);
        me.te[b] = std::max(me.te[b], t.te[b]);
    }
    for (auto& c : corps) c.shares.erase(target);
    t.alive = false;
    t.parent = corp;
    rebuildDerived();
    me.netWorth = computeNetWorth(corp);
    addLog(Chan::News, Sev::Good, corp == player, NONE, me.name + " completes its takeover of " + t.name + ".");
    return Result::good(t.name + " is now part of " + me.name + ". Its ships need your pilots before they will fly.", 0, cost);
}

void World::corporateDaily() {
    Corp& c = pc();
    if (!c.listed || now < c.nextReview) return;
    Money nav = c.netWorth / std::max<int64_t>(1, c.sharesOut);
    double growth = c.reviewBase > 0 ? (double)nav / (double)c.reviewBase - 1.0 : 0.0;
    float delta = (float)std::clamp(250.0 * (growth - c.boardTarget), -30.0, 20.0);
    c.board = std::clamp(c.board + delta, 0.0f, 100.0f);
    char g[32];
    std::snprintf(g, sizeof g, "%+.1f%%", growth * 100.0);
    std::string verdict = delta >= 0 ? "pleased" : c.board > 30 ? "concerned" : "angry";
    addLog(Chan::Corp, delta >= 0 ? Sev::Good : Sev::Warn, true, NONE,
           "Board review: net asset value per share " + std::string(g) + " over 30 days against a target of +" +
               std::to_string((int)std::lround(c.boardTarget * 100)) + "%. The board is " + verdict + " (confidence " +
               std::to_string((int)c.board) + ").");
    if (c.board < 10) {
        // the board acts: it forces a payout and replaces the CFO
        Money forced = std::max<Money>(0, c.wallet / 20);
        if (forced > 0) declareDividend(*this, player, forced);
        if (c.exec[SEAT_CFO] != NONE) {
            Person& p = people[c.exec[SEAT_CFO]];
            p.employer = NONE;
            p.alive = false;
            c.exec[SEAT_CFO] = NONE;
            addLog(Chan::Corp, Sev::Bad, true, NONE, "The board has dismissed your CFO, " + p.name + ".");
        }
        addLog(Chan::News, Sev::Bad, true, NONE, "Shareholders of " + c.name + " force a payout after a poor quarter.");
        c.board = 30;
    }
    if (growth > c.boardTarget * 2) c.boardTarget = std::min(0.15f, c.boardTarget + 0.01f);
    else if (growth < 0) c.boardTarget = std::max(0.04f, c.boardTarget - 0.01f);
    c.reviewBase = nav;
    c.nextReview = now + 30 * MINUTES_PER_DAY;
}

Result corporateCommand(World& w, const Command& c) {
    switch (c.type) {
        case Cmd::GoPublic: return w.goPublic(c.corp, c.f);
        case Cmd::DeclareDividend: return declareDividend(w, c.corp, c.price);
        case Cmd::Buyback: return buyback(w, c.corp, c.qty);
        case Cmd::Merge: return w.merge(c.corp, c.a);
        default: break;
    }
    return Result::fail("Unknown command.");
}

}  // namespace nb
