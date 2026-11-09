// NebulaOne - capital markets: commodity futures on the Charter hubs,
// corporate bonds, short selling, takeover defences (poison pills, white
// knights, greenmail) and an underwriting desk of your own.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

static const double BOND_BASE[5] = {0.010, 0.014, 0.019, 0.026, 0.034};  // coupon per 30 days by rating
static constexpr double FUT_MARGIN = 0.15, FUT_MAINT = 0.08, FUT_FEE = 0.002, FUT_SPREAD = 0.005, FUT_CARRY = 0.0015;
static constexpr double SHORT_MARGIN = 0.50, SHORT_MAINT = 0.25, SHORT_FEE = 0.005, SHORT_BORROW = 0.0005;
static constexpr int MAX_FUTURES = 24, MAX_SHORTS = 12;
static constexpr double PILL_TRIGGER = 0.25;  // a holder passing a quarter of the shares sets off an armed pill

bool futuresTradeable(int i) {
    if (i < 0 || i >= ITEM_COUNT) return false;
    ItemCat c = item(i).cat;
    return c == ItemCat::Mineral || c == ItemCat::Good || c == ItemCat::Component;
}

static int daysLeft(const World& w, Tick expiry) { return std::max(0, (int)std::ceil((double)(expiry - w.now) / MINUTES_PER_DAY)); }
static Money scale(Money m, double f) { return (Money)std::llround((double)m * f); }

// ------------------------------------------------------------------ futures --
// The hub's average over the last two weeks: where its price tends to come back to.
static double meanPrice(const World& w, Id hub, int it, double spot) {
    const Book* bk = w.stations[hub].findBook(it);
    if (!bk || bk->hist.size() < 3) return spot;
    int n = std::min<int>(14, (int)bk->hist.size());
    double sum = 0;
    for (int i = (int)bk->hist.size() - n; i < (int)bk->hist.size(); i++) sum += bk->hist[i].close * (double)CR;
    return sum / n;
}

// The forward price: today's spot for delivery now, the hub's two-week average further out, plus carry.
Money World::futuresPrice(Id hub, int it, int days) const {
    double spot = (double)refPrice(hub, it);
    double e = std::exp(-days / 5.0);
    double fwd = spot * e + meanPrice(*this, hub, it, spot) * (1.0 - e);
    return std::max<Money>(1, (Money)std::llround(fwd * (1.0 + FUT_CARRY * days)));
}

static void settleFuture(World& w, Future& f, Money exitPx, const std::string& why) {
    Money pnl = (exitPx - f.price) * f.qty * (f.isLong ? 1 : -1);
    f.result = pnl;
    f.open = false;
    const std::string what = std::string(f.isLong ? "Long " : "Short ") + fmtInt(f.qty) + " " + item(f.item).name;
    w.pay(f.corp, f.margin, Cat::Margin, "Futures margin returned: " + what);
    if (pnl != 0) w.pay(f.corp, pnl, Cat::Futures, what + " " + why);
    if (w.corps[f.corp].player)
        w.addLog(Chan::Corp, pnl >= 0 ? Sev::Good : Sev::Warn, true, w.stations[f.hub].system,
                 what + " futures " + why + ": " + (pnl >= 0 ? "made " : "lost ") + fmtMoney(std::llabs(pnl)) + ".");
}

Result World::openFuture(Id corp, Id hub, int it, int64_t qty, bool isLong, int days) {
    if (hub < 0 || hub >= (Id)stations.size() || std::find(hubs.begin(), hubs.end(), hub) == hubs.end())
        return Result::fail("Futures trade on the Charter hubs only.");
    if (!futuresTradeable(it)) return Result::fail("The Exchange lists futures on minerals, goods and components.");
    if (days != 7 && days != 14 && days != 30) return Result::fail("Pick a 7, 14 or 30-day contract.");
    if (qty <= 0) return Result::fail("Enter a quantity.");
    int open = 0;
    for (auto& f : futures) open += f.open && f.corp == corp;
    if (open >= MAX_FUTURES) return Result::fail("The Exchange allows " + std::to_string(MAX_FUTURES) + " open contracts per corporation.");
    Corp& c = corps[corp];
    Money fwd = futuresPrice(hub, it, days);
    Money px = std::max<Money>(1, scale(fwd, isLong ? 1.0 + FUT_SPREAD : 1.0 - FUT_SPREAD));
    Money notional = px * qty;
    Money cap = std::max<Money>(20000000 * CR, c.netWorth / 2);
    if (notional > cap) return Result::fail("One contract can be worth at most " + fmtMoneyShort(cap) + " (half your net worth).");
    Money margin = scale(notional, FUT_MARGIN), fee = scale(notional, FUT_FEE);
    if (c.wallet < margin + fee) return Result::fail("You need " + fmtMoney(margin + fee) + ": 15% margin and the 0.2% fee.");
    Future f;
    f.id = futures.empty() ? 0 : futures.back().id + 1;
    f.corp = corp;
    f.hub = hub;
    f.item = it;
    f.qty = qty;
    f.isLong = isLong;
    f.price = px;
    f.margin = margin;
    f.opened = now;
    f.expiry = now + (Tick)days * MINUTES_PER_DAY;
    const std::string what = std::string(isLong ? "long " : "short ") + fmtInt(qty) + " " + item(it).name;
    pay(corp, -margin, Cat::Margin, "Futures margin: " + what);
    pay(corp, -fee, Cat::Futures, "Exchange fee: " + what);
    futures.push_back(f);
    return Result::good("Opened " + what + " at " + fmtMoney(px) + " for delivery in " + std::to_string(days) +
                            " days. It settles in cash against the price at " + stations[hub].name + ".",
                        qty, notional);
}

Result World::closeFuture(Id corp, Id id) {
    for (auto& f : futures) {
        if (f.id != id) continue;
        if (f.corp != corp || !f.open) return Result::fail("That contract is not open.");
        Money cur = futuresPrice(f.hub, f.item, daysLeft(*this, f.expiry));
        Money exitPx = scale(cur, f.isLong ? 1.0 - FUT_SPREAD : 1.0 + FUT_SPREAD);
        settleFuture(*this, f, exitPx, "closed early");
        return Result::good("Closed. " + std::string(f.result >= 0 ? "Profit " : "Loss ") + fmtMoney(std::llabs(f.result)) + ".", 0, f.result);
    }
    return Result::fail("No such contract.");
}

// -------------------------------------------------------------------- bonds --
double World::bondRate(Id corp, int days) const {
    return BOND_BASE[std::clamp(corps[corp].rating, 0, 4)] + 0.0004 * days / 30.0;
}

Money World::bondsIssued(Id corp) const {
    Money v = 0;
    for (auto& b : bonds)
        if (!b.done && b.issuer == corp) v += b.units * BOND_UNIT;
    return v;
}

Money World::bondCapacity(Id corp) const {
    const Corp& c = corps[corp];
    if (c.rating >= 4) return 0;
    double frac = c.listed ? 0.40 : 0.30;
    Money lim = scale(std::max<Money>(0, c.netWorth), frac) - bondsIssued(corp);
    return std::max<Money>(0, lim);
}

Result World::issueBond(Id corp, int64_t units, int days) {
    Corp& c = corps[corp];
    if (std::find(std::begin(BOND_TERMS), std::end(BOND_TERMS), days) == std::end(BOND_TERMS))
        return Result::fail("Bonds run 30, 60, 90 or 180 days.");
    if (units <= 0 || units > 500) return Result::fail("Issue between 1 and 500 bonds (1M cr each).");
    if (c.rating >= 4) return Result::fail("No one will buy your paper at an E rating. Repair your credit first.");
    Money face = units * BOND_UNIT;
    Money capLeft = bondCapacity(corp);
    if (face > capLeft)
        return Result::fail("You can issue " + fmtMoneyShort(capLeft) + " more: bonds outstanding are capped at " +
                            (c.listed ? std::string("40%") : std::string("30%")) + " of net worth.");
    Bond b;
    b.id = bonds.empty() ? 0 : bonds.back().id + 1;
    b.issuer = corp;
    b.units = units;
    b.rate30 = bondRate(corp, days);
    b.issued = now;
    b.maturity = now + (Tick)days * MINUTES_PER_DAY;
    b.held[NONE] = units;
    Money fee = face / 100;
    pay(corp, face, Cat::Bonds, "Bond issue: " + fmtInt(units) + " x 1M cr, " + std::to_string(days) + " days");
    pay(corp, -fee, Cat::Coupons, "Bond underwriting fee (1%)");
    bonds.push_back(b);
    char r[32];
    std::snprintf(r, sizeof r, "%.2f%%", b.rate30 * 100);
    if (c.player)
        addLog(Chan::Corp, Sev::Info, true, NONE,
               "Bonds placed: " + fmtMoneyShort(face) + " for " + std::to_string(days) + " days at " + r +
                   " per 30 days. Coupons are paid every 30 days, the principal at maturity.");
    return Result::good("Raised " + fmtMoney(face - fee) + " at " + r + " per 30 days, repayable in one sum in " + std::to_string(days) +
                            " days.",
                        units, face);
}

Result World::buyBond(Id corp, Id id, int64_t units) {
    for (auto& b : bonds) {
        if (b.id != id) continue;
        if (b.done) return Result::fail("That issue has been repaid.");
        if (units <= 0) return Result::fail("Enter a number of bonds.");
        if (b.issuer == corp) {
            // calling your own bonds back from the public at 102
            int64_t pub = b.heldBy(NONE);
            if (units > pub) return Result::fail("The public holds " + fmtInt(pub) + " of these bonds.");
            Money face = units * BOND_UNIT, prem = face / 50;
            if (corps[corp].wallet < face + prem) return Result::fail("Calling them costs " + fmtMoney(face + prem) + ".");
            pay(corp, -face, Cat::Bonds, "Bonds called early: " + fmtInt(units));
            pay(corp, -prem, Cat::Coupons, "Early redemption premium (2%)");
            b.held[NONE] -= units;
            if (b.held[NONE] <= 0) b.held.erase(NONE);
            b.units -= units;
            if (b.units <= 0) b.done = true;
            return Result::good("Called " + fmtInt(units) + " bonds at 102.", units, face + prem);
        }
        if (b.onOffer <= 0 || b.offerEnds <= now) return Result::fail("That issue is no longer on offer.");
        if (units > b.onOffer) return Result::fail("Only " + fmtInt(b.onOffer) + " bonds are left on offer.");
        Money cost = units * BOND_UNIT;
        if (corps[corp].wallet < cost) return Result::fail("That costs " + fmtMoney(cost) + ".");
        pay(corp, -cost, Cat::Bonds, "Bought " + fmtInt(units) + " " + corps[b.issuer].ticker + " bonds");
        b.onOffer -= units;
        b.held[NONE] -= units;
        if (b.held[NONE] <= 0) b.held.erase(NONE);
        b.held[corp] += units;
        return Result::good("Bought " + fmtInt(units) + " " + corps[b.issuer].ticker + " bonds at par.", units, cost);
    }
    return Result::fail("No such bond.");
}

static Money bondBid(const World& w, const Bond& b) {
    return scale(BOND_UNIT, 0.985 - 0.02 * std::clamp(w.corps[b.issuer].rating, 0, 4));
}

Result World::sellBond(Id corp, Id id, int64_t units) {
    for (auto& b : bonds) {
        if (b.id != id) continue;
        if (b.done) return Result::fail("That issue has been repaid.");
        int64_t h = b.heldBy(corp);
        if (units <= 0 || units > h) return Result::fail("You hold " + fmtInt(h) + " of these bonds.");
        Money px = bondBid(*this, b);
        pay(corp, px * units, Cat::Bonds, "Sold " + fmtInt(units) + " " + corps[b.issuer].ticker + " bonds at " + fmtMoney(px));
        b.held[corp] -= units;
        if (b.held[corp] <= 0) b.held.erase(corp);
        b.held[NONE] += units;
        return Result::good("Sold " + fmtInt(units) + " bonds for " + fmtMoney(px * units) + ".", units, px * units);
    }
    return Result::fail("No such bond.");
}

static void payHolders(World& w, Bond& b, Money total, Cat cat, const std::string& memo) {
    if (b.units <= 0) return;
    for (auto& [h, n] : b.held)
        if (h != NONE && n > 0) w.pay(h, (Money)((__int128)total * n / b.units), cat, memo);
}

static void bondDefault(World& w, Bond& b) {
    Corp& is = w.corps[b.issuer];
    Money claim = b.units * BOND_UNIT + b.owed;
    Money rec = std::min<Money>(std::max<Money>(0, is.wallet) / 2, claim * 4 / 10);
    if (rec > 0) {
        w.pay(b.issuer, -rec, Cat::Bonds, "Bondholders' recovery after default");
        payHolders(w, b, rec, Cat::Bonds, "Recovered from " + is.name + "'s defaulted bonds");
    }
    b.done = true;
    b.defaulted = true;
    is.rating = 4;
    for (auto& [h, n] : b.held)
        if (h != NONE && h != b.issuer) w.addStanding(h, b.issuer, -3.0f);
    bool mine = is.player || b.heldBy(w.player) > 0;
    if (is.player) is.board = std::max(0.0f, is.board - 25.0f);
    w.addLog(Chan::News, Sev::Bad, mine, NONE,
             is.name + " defaults on " + fmtMoneyShort(b.units * BOND_UNIT) + " of bonds. Holders recover " + fmtMoneyShort(rec) + ".");
}

// ------------------------------------------------------------------- shorts --
int64_t World::corpHeldShares(Id target) const {
    int64_t n = 0;
    for (auto& c : corps) {
        auto it = c.shares.find(target);
        if (it != c.shares.end()) n += it->second;
    }
    return n;
}

int64_t World::lendableShares(Id target) const {
    const Corp& t = corps[target];
    int64_t pub = (t.player ? t.publicFloat : t.sharesOut) - corpHeldShares(target);
    int64_t lend = pub * 3 / 10;
    for (auto& s : shorts)
        if (s.open && s.target == target) lend -= s.qty;
    return std::max<int64_t>(0, lend);
}

static void coverShort(World& w, ShortPos& s, Money exitPx, const std::string& why) {
    Money pnl = (s.price - exitPx) * s.qty;
    s.result = pnl;
    s.open = false;
    const std::string what = "Short " + fmtInt(s.qty) + " " + w.corps[s.target].ticker;
    w.pay(s.corp, s.margin, Cat::Margin, "Short margin returned: " + what);
    if (pnl != 0) w.pay(s.corp, pnl, Cat::Futures, what + " " + why);
    if (w.corps[s.corp].player)
        w.addLog(Chan::Corp, pnl >= 0 ? Sev::Good : Sev::Warn, true, NONE,
                 what + " " + why + ": " + (pnl >= 0 ? "made " : "lost ") + fmtMoney(std::llabs(pnl)) + ".");
}

Result World::openShort(Id corp, Id target, int64_t qty) {
    if (target < 0 || target >= (Id)corps.size() || !corps[target].alive) return Result::fail("That corporation is not listed.");
    Corp& t = corps[target];
    if (target == corp) return Result::fail("You cannot short your own corporation.");
    if (t.player) return Result::fail("The Exchange does not lend that stock.");
    if (qty <= 0) return Result::fail("Enter a number of shares.");
    int open = 0;
    for (auto& s : shorts) open += s.open && s.corp == corp;
    if (open >= MAX_SHORTS) return Result::fail("The Exchange allows " + std::to_string(MAX_SHORTS) + " open short positions.");
    int64_t lend = lendableShares(target);
    if (qty > lend) return Result::fail("The Exchange can lend " + fmtInt(lend) + " " + t.ticker + " shares.");
    double impact = 2.0 * (double)qty / (double)t.sharesOut;
    Money px = std::max<Money>(1, scale(t.sharePrice, 1.0 - impact / 2));
    Money proceeds = px * qty;
    Money margin = scale(proceeds, SHORT_MARGIN), fee = scale(proceeds, SHORT_FEE);
    if (corps[corp].wallet < margin + fee) return Result::fail("You need " + fmtMoney(margin + fee) + ": 50% margin and the 0.5% fee.");
    ShortPos s;
    s.id = shorts.empty() ? 0 : shorts.back().id + 1;
    s.corp = corp;
    s.target = target;
    s.qty = qty;
    s.price = px;
    s.margin = margin;
    s.opened = now;
    pay(corp, -margin, Cat::Margin, "Short margin: " + fmtInt(qty) + " " + t.ticker);
    pay(corp, -fee, Cat::Futures, "Stock loan arrangement fee: " + t.ticker);
    t.sharePrice = std::max<Money>(1, scale(t.sharePrice, 1.0 - impact));
    shorts.push_back(s);
    return Result::good("Sold short " + fmtInt(qty) + " " + t.ticker + " at " + fmtMoney(px) +
                            ". You profit if the price falls; the loan costs 0.05% a day and you owe any dividend.",
                        qty, proceeds);
}

Result World::closeShort(Id corp, Id id) {
    for (auto& s : shorts) {
        if (s.id != id) continue;
        if (s.corp != corp || !s.open) return Result::fail("That position is not open.");
        Corp& t = corps[s.target];
        double impact = 2.0 * (double)s.qty / (double)t.sharesOut;
        Money px = scale(t.sharePrice, 1.0 + impact / 2);
        t.sharePrice = scale(t.sharePrice, 1.0 + impact);
        coverShort(*this, s, px, "covered");
        return Result::good("Covered at " + fmtMoney(px) + ". " + (s.result >= 0 ? "Profit " : "Loss ") + fmtMoney(std::llabs(s.result)) + ".",
                            0, s.result);
    }
    return Result::fail("No such position.");
}

// ------------------------------------------------------- takeover defences --
void World::firePill(Id target, Id raider) {
    Corp& t = corps[target];
    int64_t S = t.sharesOut;
    auto rit = corps[raider].shares.find(target);
    int64_t R = rit == corps[raider].shares.end() ? 0 : rit->second;
    int64_t others = S - R;
    if (others <= 0) return;
    int64_t N = S;  // as many new shares again, to everyone but the raider
    int64_t corpGiven = 0;
    for (auto& h : corps) {
        if (h.id == raider || h.id == target) continue;
        auto it = h.shares.find(target);
        if (it == h.shares.end() || it->second <= 0) continue;
        int64_t add = (int64_t)((__int128)N * it->second / others);
        it->second += add;
        corpGiven += add;
    }
    if (t.player) {
        int64_t founder = S - t.publicFloat;  // your own holding
        int64_t founderAdd = (int64_t)((__int128)N * founder / others);
        t.publicFloat = (S + N) - (founder + founderAdd);
        t.reviewBase = (Money)((__int128)t.reviewBase * S / (S + N));
    }
    t.sharesOut = S + N;
    t.sharePrice = std::max<Money>(1, (Money)((__int128)t.sharePrice * S / (S + N)));
    for (auto& m : t.shareHist) m = (Money)((__int128)m * S / (S + N));
    t.poisonPill = false;
    t.defenceUsed |= 1;
    double before = 100.0 * R / S, after = 100.0 * R / (S + N);
    char b[96];
    std::snprintf(b, sizeof b, "%.1f%% to %.1f%%", before, after);
    addLog(Chan::News, Sev::Warn, t.player || corps[raider].player, NONE,
           t.name + "'s poison pill fires: " + fmtInt(N) + " new shares go to every holder but " + corps[raider].name +
               ", whose stake is diluted from " + b + ".");
}

Result World::adoptPill(Id corp) {
    Corp& c = corps[corp];
    if (!c.listed) return Result::fail("Only a listed corporation needs a poison pill.");
    if (c.poisonPill) return Result::fail("Your pill is already in place.");
    Money fee = 2000000 * CR;
    if (c.wallet < fee) return Result::fail("The lawyers want " + fmtMoney(fee) + ".");
    pay(corp, -fee, Cat::Other, "Shareholder rights plan (poison pill): legal fees");
    c.poisonPill = true;
    c.board = std::max(0.0f, c.board - 4.0f);
    addLog(Chan::News, Sev::Info, true, NONE,
           c.name + " adopts a shareholder rights plan: any outsider passing 25% will be diluted. Some shareholders call it entrenchment.");
    return Result::good("Poison pill adopted. A raider passing 25% of your shares sets it off. Board confidence -4.");
}

Result World::whiteKnight(Id corp, Id knight) {
    Corp& c = corps[corp];
    if (!c.listed) return Result::fail("You are not listed.");
    if (c.raider == NONE) return Result::fail("No one is raiding you.");
    if (knight < 0 || knight >= (Id)corps.size() || knight == c.raider || knight == corp || !corps[knight].alive || corps[knight].player)
        return Result::fail("Pick a friendly corporation other than the raider.");
    Corp& k = corps[knight];
    if (standing(knight, corp) < 1.0f) return Result::fail(k.name + " is not friendly enough to step in (standing 1.0 or better).");
    int64_t avail = c.publicFloat - corpHeldShares(corp);
    if (avail <= 0) return Result::fail("There are no public shares left to take.");
    Money cost = scale(c.sharePrice * avail, 1.05);
    if (k.wallet < cost) return Result::fail(k.name + " cannot afford the " + fmtMoneyShort(cost) + " block.");
    pay(knight, -cost, Cat::ShareTrade, "White knight block in " + c.name);
    k.shares[corp] += avail;
    c.sharePrice = scale(c.sharePrice, 1.05);
    addStanding(knight, corp, -1.5f);
    if (c.raidDeadline == 0 || c.raidDeadline > now + 3 * MINUTES_PER_DAY) c.raidDeadline = now + 3 * MINUTES_PER_DAY;
    addLog(Chan::News, Sev::Good, true, NONE,
           k.name + " steps in as a white knight for " + c.name + ", taking the remaining " + fmtInt(avail) + " public shares. " +
               corps[c.raider].name + " is shut out.");
    return Result::good(k.name + " takes the float: the raider cannot buy more and will give up within days. Standing with " + k.name +
                        " -1.5 for the favour.");
}

Result World::payGreenmail(Id corp) {
    Corp& c = corps[corp];
    if (c.raider == NONE) return Result::fail("No one is raiding you.");
    Corp& r = corps[c.raider];
    auto it = r.shares.find(corp);
    int64_t stake = it == r.shares.end() ? 0 : it->second;
    if (stake <= 0) { c.raider = NONE; return Result::fail("The raider holds nothing any more."); }
    Money cost = scale(c.sharePrice * stake, 1.25);
    if (c.wallet < cost) return Result::fail("Buying " + r.name + " out at a 25% premium costs " + fmtMoney(cost) + ".");
    pay(corp, -cost, Cat::Equity, "Greenmail: bought back " + r.name + "'s stake");
    pay(r.id, cost, Cat::ShareTrade, "Greenmail from " + c.name);
    r.shares.erase(it);
    c.sharesOut -= stake;
    c.publicFloat -= stake;
    c.board = std::max(0.0f, c.board - 6.0f);
    addStanding(r.id, corp, 1.0f);
    addLog(Chan::News, Sev::Info, true, NONE,
           c.name + " pays " + r.name + " " + fmtMoneyShort(cost) + " to go away. The shares are cancelled; other shareholders grumble.");
    c.raider = NONE;
    c.raidDeadline = 0;
    return Result::good("Paid " + fmtMoney(cost) + ". " + r.name + " is gone and its " + fmtInt(stake) + " shares are cancelled. Board -6.");
}

// An AI corporation reacting to the player building a stake in it.
void World::aiDefences(Corp& c) {
    auto it = pc().shares.find(c.id);
    if (it == pc().shares.end() || it->second <= 0) return;
    int64_t held = it->second;
    double p = (double)held / (double)c.sharesOut;
    if (p >= 0.15 && !c.poisonPill && !(c.defenceUsed & 1) && rng.chance(c.arch == Arch::Industrialist || c.arch == Arch::Trader ? 0.45 : 0.25)) {
        c.poisonPill = true;
        addLog(Chan::News, Sev::Warn, true, NONE,
               c.name + " adopts a poison pill after " + pc().name + " builds a " + std::to_string((int)std::lround(p * 100)) +
                   "% stake. Anyone passing 25% will be diluted.");
    }
    if (p >= 0.30 && p < 0.5 && !(c.defenceUsed & 2) && rng.chance(0.35)) {
        int64_t floatLeft = c.sharesOut - corpHeldShares(c.id);
        int64_t need = floatLeft - (c.sharesOut / 2 - held);  // what the knight must take so you cannot pass half
        if (need <= 0) return;
        Id best = NONE;
        float bestSt = 0.5f;
        for (auto& k : corps) {
            if (!k.alive || k.player || k.id == c.id || k.parent != NONE) continue;
            if (k.wallet < scale(c.sharePrice * need, 1.1)) continue;
            float st = standing(k.id, c.id) + (k.arch == c.arch ? 0.5f : 0.0f);
            if (st > bestSt) { bestSt = st; best = k.id; }
        }
        if (best == NONE) return;
        Corp& k = corps[best];
        Money cost = scale(c.sharePrice * need, 1.1);
        pay(best, -cost, Cat::ShareTrade, "White knight stake in " + c.name);
        k.shares[c.id] += need;
        c.sharePrice = scale(c.sharePrice, 1.05);
        c.defenceUsed |= 2;
        addLog(Chan::News, Sev::Warn, true, NONE,
               k.name + " rides to the rescue of " + c.name + " as a white knight, buying " + fmtInt(need) +
                   " shares. You can no longer reach a majority on the open market; " + k.name + " itself is still for sale.");
    }
}

// A rival quietly building a stake in the listed player, then demanding greenmail.
void World::aiRaid() {
    Corp& p = pc();
    if (!p.listed) { p.raider = NONE; return; }
    if (p.raider == NONE) {
        if (!rng.chance(0.012)) return;
        Money cap = p.sharePrice * p.sharesOut;
        Id best = NONE;
        float worst = 1e9f;
        for (auto& c : corps) {
            if (!c.alive || c.player || c.parent != NONE) continue;
            if (c.arch == Arch::Miner) continue;
            if (c.wallet < cap / 6) continue;
            float st = standing(c.id, player) + rng.range(-1.0, 1.0);
            if (st < worst) { worst = st; best = c.id; }
        }
        if (best == NONE) return;
        p.raider = best;
        p.raidDeadline = 0;
        return;
    }
    Corp& r = corps[p.raider];
    if (!r.alive) { p.raider = NONE; p.raidDeadline = 0; return; }
    auto it = r.shares.find(player);
    int64_t stake = it == r.shares.end() ? 0 : it->second;
    double frac = (double)stake / (double)p.sharesOut;
    if (p.raidDeadline == 0) {
        double goal = 0.16 + 0.12 * ((p.raider * 2654435761u) % 100) / 100.0;  // each raider has its appetite
        if (frac < goal) {
            int64_t q = (int64_t)(p.sharesOut * rng.range(0.008, 0.02));
            Result res = tradeShares(r.id, player, q, true);
            if (!res.ok && frac < 0.05) { p.raider = NONE; return; }  // no float or no money: it drops the idea
            if (p.raider == NONE) return;  // a pill fired and saw it off
            it = r.shares.find(player);
            stake = it == r.shares.end() ? 0 : it->second;
            frac = (double)stake / (double)p.sharesOut;
        }
        if (frac >= 0.10) {
            p.raidDeadline = now + 30 * MINUTES_PER_DAY;
            addLog(Chan::Corp, Sev::Bad, true, NONE,
                   r.name + " has built a " + std::to_string((int)std::lround(frac * 100)) +
                       "% stake in you and is working on your board. It wants buying out at a premium (greenmail) within 30 days. "
                       "Boardroom: pay it, adopt a poison pill, or find a white knight.");
        }
        return;
    }
    // agitating
    p.board = std::max(0.0f, p.board - 0.35f);
    if (now >= p.raidDeadline) {
        p.board = std::max(0.0f, p.board - 10.0f);
        if (stake > 0) tradeShares(r.id, player, stake, false);
        addStanding(r.id, player, -2.0f);
        addLog(Chan::News, Sev::Warn, true, NONE,
               r.name + " loses patience with " + p.name + ", dumps its stake and walks away. The board is shaken.");
        p.raider = NONE;
        p.raidDeadline = 0;
    }
}

// --------------------------------------------------------------- underwriting --
Result World::setUnderwriting(Id corp, const Underwriting& u) {
    if (corp != player) return Result::fail("Only you run an underwriting desk.");
    Corp& c = corps[corp];
    if (!u.open) {
        if (!uw.open) return Result::fail("The desk is already closed to new business.");
        uw.open = false;
        return Result::good("The desk writes no new policies. The " + fmtInt((int64_t)uw.book.size()) + " live ones run off.");
    }
    if (u.rateMul < 0.6f || u.rateMul > 1.6f) return Result::fail("Price your cover between 0.6x and 1.6x the Charter rate.");
    if (u.maxRisk < 5 || u.maxRisk > 100) return Result::fail("Set a risk limit between 5 and 100.");
    if (u.capacity < 5000000 * CR) return Result::fail("Write at least 5M cr of cover.");
    if (c.charterStanding <= -5) return Result::fail("Outlaws cannot hold a Charter underwriting licence.");
    Money reserve = std::max<Money>(0, c.wallet);
    if (u.capacity > reserve * 4)
        return Result::fail("The Charter lets you write cover up to four times your cash: at most " + fmtMoneyShort(reserve * 4) + ".");
    if (!uw.licensed) {
        Money lic = 5000000 * CR;
        if (c.wallet < lic) return Result::fail("The underwriting licence costs " + fmtMoney(lic) + ".");
        pay(corp, -lic, Cat::Underwriting, "Charter underwriting licence");
        uw.licensed = true;
    }
    uw.open = true;
    uw.rateMul = u.rateMul;
    uw.maxRisk = u.maxRisk;
    uw.capacity = u.capacity;
    char b[64];
    std::snprintf(b, sizeof b, "%.2fx the Charter rate", u.rateMul);
    return Result::good("Desk open: hull cover at " + std::string(b) + " for ships working at risk " + std::to_string(u.maxRisk) +
                        " or lower, up to " + fmtMoneyShort(u.capacity) + " in force.");
}

void World::uwClaim(const Ship& s) {
    auto it = uw.book.find(s.id);
    if (it == uw.book.end()) return;
    auto [cover, until] = it->second;
    uw.book.erase(it);
    if (until <= now || corps[s.corp].player) return;
    pay(player, -cover, Cat::Underwriting, "Hull claim: " + corps[s.corp].name + " " + shipDef(s.def).name);
    pay(s.corp, cover, Cat::InsurancePayout, "Hull claim paid by " + pc().name);
    uw.claims += cover;
    uw.paidClaims++;
    if (cover >= 5000000 * CR)
        addLog(Chan::Corp, Sev::Warn, true, NONE,
               "Your desk pays a " + fmtMoneyShort(cover) + " claim on " + corps[s.corp].name + "'s " + shipDef(s.def).name + ".");
}

static void uwDaily(World& w) {
    auto& uw = w.uw;
    for (auto it = uw.book.begin(); it != uw.book.end();)
        it = (it->second.second <= w.now || !w.ships[it->first].alive) ? uw.book.erase(it) : std::next(it);
    if (!uw.open) return;
    Money exposure = uw.exposure();
    Corp& me = w.pc();
    if (me.wallet < exposure / 10) {
        if ((w.now / MINUTES_PER_DAY) % 5 == 0)
            w.addLog(Chan::Corp, Sev::Warn, true, NONE, "Your underwriting desk is short of reserves (10% of cover in force) and writes nothing new.");
        return;
    }
    double demand = 0.03 * std::clamp((1.7 - uw.rateMul) / 0.7, 0.05, 1.5);
    Money written = 0;
    int n = 0;
    for (auto& s : w.ships) {
        if (!s.alive || w.corps[s.corp].player || uw.book.count(s.id)) continue;
        if (!w.rng.chance(demand)) continue;
        if (w.shipRisk(s) > uw.maxRisk) continue;
        Money cover = w.shipValue(s) * 70 / 100;
        if (exposure + cover > uw.capacity) continue;
        Money prem = scale(w.insurancePremium(s), uw.rateMul);
        if (w.corps[s.corp].wallet < prem * 4) continue;
        uw.book[s.id] = {cover, w.now + 30 * MINUTES_PER_DAY};
        exposure += cover;
        w.pay(s.corp, -prem, Cat::Insurance, "Hull cover");
        written += prem;
        n++;
    }
    if (written > 0) {
        w.pay(w.player, written, Cat::Underwriting, "Premiums: " + std::to_string(n) + " new hull policies");
        uw.premiums += written;
        uw.policies += n;
    }
}

// ---------------------------------------------------------------- the day --
void World::capitalDaily() {
    // futures: settle what has expired, mark the rest to market
    for (auto& f : futures) {
        if (!f.open) continue;
        if (now >= f.expiry) { settleFuture(*this, f, refPrice(f.hub, f.item), "settled at expiry"); continue; }
        Money cur = futuresPrice(f.hub, f.item, daysLeft(*this, f.expiry));
        Money loss = (f.price - cur) * f.qty * (f.isLong ? 1 : -1);
        Money equity = f.margin - loss, notional = cur * f.qty;
        if (equity >= scale(notional, FUT_MAINT)) continue;
        Money call = scale(notional, FUT_MARGIN) - equity;
        if (corps[f.corp].wallet >= call) {
            pay(f.corp, -call, Cat::Margin, "Futures margin call: " + item(f.item).name);
            f.margin += call;
            if (corps[f.corp].player)
                addLog(Chan::Corp, Sev::Warn, true, NONE, "Margin call on your " + item(f.item).name + " futures: " + fmtMoney(call) + " posted.");
        } else {
            settleFuture(*this, f, scale(cur, f.isLong ? 1.0 - FUT_SPREAD : 1.0 + FUT_SPREAD), "closed out on a missed margin call");
        }
    }
    // shorts: the stock-loan fee, margin, and targets that vanished
    for (auto& s : shorts) {
        if (!s.open) continue;
        Corp& t = corps[s.target];
        if (!t.alive) {
            Money px = t.parent != NONE ? t.sharePrice : 0;
            coverShort(*this, s, px, t.parent != NONE ? "closed: the company was taken over" : "closed: the company is gone");
            continue;
        }
        Money value = t.sharePrice * s.qty;
        pay(s.corp, -std::max<Money>(1, scale(value, SHORT_BORROW)), Cat::Futures, "Stock loan fee: " + t.ticker);
        Money equity = s.margin - (t.sharePrice - s.price) * s.qty;
        if (equity >= scale(value, SHORT_MAINT)) continue;
        Money call = scale(value, SHORT_MARGIN) - equity;
        if (corps[s.corp].wallet >= call) {
            pay(s.corp, -call, Cat::Margin, "Short margin call: " + t.ticker);
            s.margin += call;
            if (corps[s.corp].player) addLog(Chan::Corp, Sev::Warn, true, NONE, "Margin call on your " + t.ticker + " short: " + fmtMoney(call) + " posted.");
        } else {
            double impact = 2.0 * (double)s.qty / (double)t.sharesOut;
            Money px = scale(t.sharePrice, 1.0 + impact / 2);
            t.sharePrice = scale(t.sharePrice, 1.0 + impact);
            coverShort(*this, s, px, "bought in on a missed margin call");
        }
    }
    // bonds: coupons, principal, arrears and defaults
    for (auto& b : bonds) {
        if (b.done) continue;
        Corp& is = corps[b.issuer];
        if (!is.alive) {
            if (is.parent != NONE) { b.issuer = is.parent; continue; }  // the acquirer takes on the debt
            bondDefault(*this, b);
            continue;
        }
        if (b.onOffer > 0 && b.offerEnds <= now) b.onOffer = 0;
        int term = (int)((b.maturity - b.issued) / (30 * MINUTES_PER_DAY));
        int due = std::min(term, (int)((now - b.issued) / (30 * MINUTES_PER_DAY)));
        while (b.coupons < due) {
            b.owed += b.coupon();
            b.coupons++;
            if (b.coupons == term) b.principalDue = true;
        }
        if (b.owed <= 0 && !b.principalDue) continue;
        Money principal = b.principalDue ? b.units * BOND_UNIT : 0;
        if (is.wallet >= b.owed + principal) {
            const std::string tk = is.ticker;
            if (b.owed > 0) {
                pay(b.issuer, -b.owed, Cat::Coupons, "Bond coupon");
                payHolders(*this, b, b.owed, Cat::Coupons, "Coupon from " + tk + " bonds");
            }
            if (principal > 0) {
                pay(b.issuer, -principal, Cat::Bonds, "Bonds repaid at maturity");
                payHolders(*this, b, principal, Cat::Bonds, tk + " bonds repaid at maturity");
                b.done = true;
                if (is.player || b.heldBy(player) > 0)
                    addLog(Chan::Corp, Sev::Good, true, NONE, tk + " bonds of " + fmtMoneyShort(principal) + " repaid at maturity.");
            }
            b.owed = 0;
            b.missed = 0;
        } else {
            b.missed++;
            if (is.player && (b.missed == 1 || b.missed % 2 == 0))
                addLog(Chan::Corp, Sev::Bad, true, NONE,
                       "You owe bondholders " + fmtMoney(b.owed + principal) + " and cannot pay. Default in " +
                           std::to_string(std::max(0, 6 - b.missed)) + " days.");
            if (b.missed == 1 && is.player) is.rating = std::min(4, is.rating + 1);
            if (b.missed > (is.player ? 5 : 2)) bondDefault(*this, b);
        }
    }
    // AI corporations raise money with bonds; the player may buy from the offer
    int offers = 0;
    for (auto& b : bonds) offers += !b.done && b.onOffer > 0 && b.offerEnds > now;
    for (auto& c : corps) {
        if (offers >= 10) break;
        if (c.player || !c.alive || c.parent != NONE || c.rating >= 4 || !rng.chance(0.012)) continue;
        if (bondsIssued(c.id) > c.netWorth / 5) continue;
        int64_t units = std::clamp<int64_t>((int64_t)(toCr(c.netWorth) * rng.range(0.02, 0.06) / 1e6), 2, 80);
        int days = BOND_TERMS[rng.irange(0, 2)];
        Bond b;
        b.id = bonds.empty() ? 0 : bonds.back().id + 1;
        b.issuer = c.id;
        b.units = units;
        b.onOffer = units;
        b.rate30 = std::round((bondRate(c.id, days) + rng.range(-0.001, 0.003)) * 10000.0) / 10000.0;
        b.issued = now;
        b.maturity = now + (Tick)days * MINUTES_PER_DAY;
        b.offerEnds = now + 6 * MINUTES_PER_DAY;
        b.held[NONE] = units;
        pay(c.id, units * BOND_UNIT, Cat::Bonds, "Bond issue");
        bonds.push_back(b);
        offers++;
    }
    if (bonds.size() > 600)
        bonds.erase(std::remove_if(bonds.begin(), bonds.end(), [&](const Bond& b) { return b.done && now - b.maturity > 60 * MINUTES_PER_DAY; }),
                    bonds.end());
    if (futures.size() > 400)
        futures.erase(std::remove_if(futures.begin(), futures.end(), [&](const Future& f) { return !f.open && now - f.expiry > 60 * MINUTES_PER_DAY; }),
                      futures.end());
    if (shorts.size() > 200)
        shorts.erase(std::remove_if(shorts.begin(), shorts.end(), [&](const ShortPos& s) { return !s.open && now - s.opened > 90 * MINUTES_PER_DAY; }),
                     shorts.end());
    // takeover defences and raids
    for (auto& c : corps)
        if (c.alive && !c.player) aiDefences(c);
    aiRaid();
    uwDaily(*this);
}

}  // namespace nb
