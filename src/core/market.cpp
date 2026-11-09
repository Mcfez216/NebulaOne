// NebulaOne - station order books, the population market makers and valuations.
//
// Every station's population quotes a four-level ladder around its reference
// price. Near activity the ladder is posted as real orders; elsewhere it is
// computed on demand ("virtual"), so a Galaxy of thousands of stations costs
// almost nothing until somebody goes there.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

namespace {
struct Elastic { double e, lo, hi, revert; };
Elastic elastic(int i) {
    if (isOre(i)) return {0.5, 0.40, 1.25, 0.15};
    if (isMineral(i)) return {0.5, 0.35, 3.0, 0.15};
    if (isGood(i)) return {0.5, 0.40, 3.0, 0.15};
    if (isComponent(i)) return {0.5, 0.45, 2.5, 0.15};
    if (isModule(i)) return {0.4, 0.70, 2.0, 0.25};
    if (isShipItem(i) || isBpc(i)) return {0.4, 0.80, 1.6, 0.3};
    if (isStructure(i)) return {0.3, 0.85, 1.4, 0.3};
    return {0, 1, 1, 0};
}
const double SELL_LVL[4] = {1.02, 1.05, 1.09, 1.15};
const double BUY_LVL[4] = {0.98, 0.95, 0.91, 0.86};
const double LVL_SHARE[4] = {0.30, 0.30, 0.25, 0.15};

bool wholeUnits(int i) { return isShipItem(i) || isBpo(i) || isBpc(i) || isModule(i) || isStructure(i); }
bool popTrades(const Station& st) {
    return !st.dead && (!st.isOutpost() || st.state == OutpostState::Online);
}
}  // namespace

static double remotePremium(const World& w, const Station& st) {
    const StarSystem& s = w.systems[st.system];
    double z = s.zone == Zone::Charter ? 0.0 : s.zone == Zone::Contract ? 0.12 : 0.30;
    return z + s.risk * 0.001;
}

Money World::refPrice(Id station, int i) const {
    const Station& st = stations[station];
    Money base = item(i).base;
    if (isBpo(i)) return base;
    double T = st.target[i];
    if (T <= 0) return (Money)std::llround(base * (1.0 + remotePremium(*this, st)));
    Elastic el = elastic(i);
    double S = std::max<double>(st.stock[i], 0.02 * T);
    double f = std::clamp(std::pow(T / S, el.e), el.lo, el.hi);
    return (Money)std::llround(base * f * (1.0 + remotePremium(*this, st)));
}

// The population's quotes for one item: up to four sell and four buy levels.
struct Level { Money price; int64_t qty; bool buy; };
static int makerLevels(const World& w, const Station& st, int i, Level out[8]) {
    double T = st.target[i];
    if (T <= 0 || !popTrades(st)) return 0;
    Money P = w.refPrice(st.id, i);
    int n = 0;
    auto mk = [&](bool buy, Money price, double q) {
        int64_t k = (int64_t)std::floor(q);
        if (k <= 0 || price <= 0) return;
        out[n++] = {price, k, buy};
    };
    if (isBpo(i)) { mk(false, P, 1000); return n; }
    double S = st.stock[i];
    bool hub = st.type == StationType::TradeHub;
    bool consumer = st.cons[i] > 0;
    bool whole = wholeUnits(i);
    double reserve = hub ? 0.2 * T : consumer ? 0.35 * T : 0.0;
    double sellable = std::max(0.0, S - reserve);
    double want = (consumer || hub) ? std::max(0.0, 1.6 * T - S) : 0.0;
    for (int k = 0; k < 4; k++) {
        double qs = whole ? (k == 0 ? std::floor(sellable) : 0) : sellable * LVL_SHARE[k];
        double qb = whole ? (k == 0 ? std::floor(want) : 0) : want * LVL_SHARE[k];
        mk(false, (Money)(P * SELL_LVL[k]), qs);
        mk(true, (Money)(P * BUY_LVL[k]), qb);
    }
    return n;
}

static void makerLadder(const World& w, const Station& st, int i, std::vector<Order>* sells, std::vector<Order>* buys, int64_t* nextId,
                        Tick now) {
    Level lv[8];
    int n = makerLevels(w, st, i, lv);
    for (int k = 0; k < n; k++) {
        Order o;
        o.id = nextId ? (*nextId)++ : 0;
        o.corp = NONE;
        o.item = i;
        o.buy = lv[k].buy;
        o.price = lv[k].price;
        o.qty = o.qty0 = lv[k].qty;
        o.placed = now;
        o.expires = now + 120;
        if (o.buy && buys) buys->push_back(o);
        if (!o.buy && sells) sells->push_back(o);
    }
}

Money World::makerQuote(Id station, int i, bool buy, int64_t* depth) const {
    Level lv[8];
    int n = makerLevels(*this, stations[station], i, lv);
    Money best = 0;
    for (int k = 0; k < n; k++)
        if (lv[k].buy == buy && (!best || (buy ? lv[k].price > best : lv[k].price < best))) best = lv[k].price;
    if (depth) {
        *depth = 0;
        for (int k = 0; k < n; k++)
            if (lv[k].buy == buy && (buy ? lv[k].price >= best * 95 / 100 : lv[k].price <= best * 105 / 100)) *depth += lv[k].qty;
    }
    return best;
}

void World::bookView(Id station, int i, std::vector<Order>& sells, std::vector<Order>& buys) const {
    sells.clear();
    buys.clear();
    const Station& st = stations[station];
    if (const Book* bk = st.findBook(i)) {
        sells = bk->sells;
        buys = bk->buys;
    }
    if (!st.makerLive) makerLadder(*this, st, i, &sells, &buys, nullptr, now);
    std::stable_sort(sells.begin(), sells.end(), [](const Order& a, const Order& b) { return a.price < b.price; });
    std::stable_sort(buys.begin(), buys.end(), [](const Order& a, const Order& b) { return a.price > b.price; });
}

Money World::bestAsk(Id station, int i, int64_t* depth, Id excl) const {
    const Station& st = stations[station];
    Money best = 0;
    if (const Book* bk = st.findBook(i))
        for (auto& o : bk->sells)
            if (!(o.corp == excl && excl != NONE) && o.qty > 0) { best = o.price; break; }
    int64_t vdepth = 0;
    Money vbest = st.makerLive ? 0 : makerQuote(station, i, false, nullptr);
    if (vbest && (!best || vbest < best)) best = vbest;
    if (depth) {
        int64_t d = 0;
        if (const Book* bk = st.findBook(i))
            for (auto& o : bk->sells) {
                if (o.corp == excl && excl != NONE) continue;
                if (o.price <= best * 105 / 100) d += o.qty; else break;
            }
        if (!st.makerLive && best) {
            Level lv[8];
            int n = makerLevels(*this, st, i, lv);
            for (int k = 0; k < n; k++)
                if (!lv[k].buy && lv[k].price <= best * 105 / 100) vdepth += lv[k].qty;
        }
        *depth = d + vdepth;
    }
    return best;
}

Money World::bestBid(Id station, int i, int64_t* depth, Id excl) const {
    const Station& st = stations[station];
    Money best = 0;
    if (const Book* bk = st.findBook(i))
        for (auto& o : bk->buys)
            if (!(o.corp == excl && excl != NONE) && o.qty > 0) { best = o.price; break; }
    Money vbest = st.makerLive ? 0 : makerQuote(station, i, true, nullptr);
    if (vbest > best) best = vbest;
    if (depth) {
        int64_t d = 0;
        if (const Book* bk = st.findBook(i))
            for (auto& o : bk->buys) {
                if (o.corp == excl && excl != NONE) continue;
                if (o.price >= best * 95 / 100) d += o.qty; else break;
            }
        if (!st.makerLive && best) {
            Level lv[8];
            int n = makerLevels(*this, st, i, lv);
            for (int k = 0; k < n; k++)
                if (lv[k].buy && lv[k].price >= best * 95 / 100) d += lv[k].qty;
        }
        *depth = d;
    }
    return best;
}

Money World::valueAt(Id station, int i) const {
    if (isBpo(i)) return item(i).base / 2;
    if (isShipItem(i)) return item(i).base;
    return refPrice(station, i) * 95 / 100;
}

// A pile bigger than the station can absorb is not worth its sticker price.
Money World::valueStock(Id station, int i, int64_t qty) const {
    if (qty <= 0) return 0;
    if (isBpo(i) || isShipItem(i)) return valueAt(station, i) * qty;
    Money ref = refPrice(station, i);
    double T = stations[station].target[i];
    int64_t liquid = T > 0 ? (int64_t)(2.0 * T) : (isModule(i) || isStructure(i) || isBpc(i) ? qty : 0);
    int64_t a = std::min(qty, liquid), b = qty - a;
    return ref * 9 / 10 * a + ref * 3 / 10 * b;
}

Money World::escrowValue(Id corp) const {
    Money v = 0;
    for (int64_t spot : corps[corp].orderSpots) {
        Id st = (Id)(spot / HANGAR_KEY);
        int i = (int)(spot % HANGAR_KEY);
        const Book* bk = stations[st].findBook(i);
        if (!bk) continue;
        for (auto& o : bk->buys)
            if (o.corp == corp) v += o.price * o.qty;
        for (auto& o : bk->sells)
            if (o.corp == corp) v += valueStock(st, i, o.qty);
    }
    return v;
}

Money World::loansOwed(Id corp) const {
    Money v = 0;
    for (auto& l : loans)
        if (l.status == 0 && l.borrower == corp) v += l.remaining;
    return v;
}

Money World::computeNetWorth(Id corp) const {
    const Corp& c = corps[corp];
    Money v = c.wallet;
    for (auto& [k, q] : c.hangar) v += valueStock((Id)(k / HANGAR_KEY), (int)(k % HANGAR_KEY), q);
    for (Id sid : c.shipList) {
        const Ship& s = ships[sid];
        if (s.alive && s.corp == corp) v += (Money)(shipValue(s) * (0.6 + 0.4 * s.hull / std::max(1.0, stats(s).hull)));
    }
    for (Id fid : c.fleetList) {
        const Fleet& f = fleets[fid];
        if (f.alive && f.corp == corp) v += fleetCargoValue(f);
    }
    for (auto& [cid, n] : c.shares) v += corps[cid].sharePrice * n;
    for (auto& l : loans) {
        if (l.status != 0) continue;
        if (l.lender == corp) v += l.remaining * 9 / 10;
        if (l.borrower == corp) v -= l.remaining;
    }
    for (auto& ct : contracts)
        if (ct.state == ContractState::Accepted && ct.contractor == corp) v += ct.collateral;
    v += escrowValue(corp);
    // outposts are worth what their kit cost, less wear
    for (Id sid : outpostIds) {
        const Station& st = stations[sid];
        if (!st.dead && st.owner == corp) v += (Money)(item(OUTPOST_KIT).base * (0.5 + 0.5 * st.hull));
    }
    // colonies: the kit, and more the bigger they grow
    for (const Colony& c : colonies)
        if (c.owner == corp && !stations[c.station].dead) v += (Money)(item(COLONY_KIT).base * (0.5 + c.pop / 40.0));
    // capital markets: margin plus open profit, bonds held less bonds owed
    for (const Future& f : futures)
        if (f.open && f.corp == corp) {
            int dl = std::max(0, (int)std::ceil((double)(f.expiry - now) / MINUTES_PER_DAY));
            v += f.margin + (futuresPrice(f.hub, f.item, dl) - f.price) * f.qty * (f.isLong ? 1 : -1);
        }
    for (const ShortPos& s : shorts)
        if (s.open && s.corp == corp) v += s.margin + (s.price - corps[s.target].sharePrice) * s.qty;
    for (const Bond& b : bonds) {
        if (b.done) continue;
        if (b.issuer == corp) v -= (b.units - b.heldBy(corp)) * BOND_UNIT + b.owed;
        else if (int64_t h = b.heldBy(corp)) v += h * BOND_UNIT * 95 / 100;
    }
    return v;
}

// ----------------------------------------------------------------- books ----
void World::sortBook(Book& bk) {
    std::stable_sort(bk.sells.begin(), bk.sells.end(), [](const Order& a, const Order& b) { return a.price < b.price; });
    std::stable_sort(bk.buys.begin(), bk.buys.end(), [](const Order& a, const Order& b) { return a.price > b.price; });
}

void World::pruneBook(Book& bk) {
    bk.sells.erase(std::remove_if(bk.sells.begin(), bk.sells.end(), [](const Order& o) { return o.qty <= 0; }), bk.sells.end());
    bk.buys.erase(std::remove_if(bk.buys.begin(), bk.buys.end(), [](const Order& o) { return o.qty <= 0; }), bk.buys.end());
}

void World::recordTrade(Book& bk, Money price, int64_t qty, bool corpTrade) {
    float p = (float)toCr(price);
    if (!bk.tradedToday) {
        bk.today = Bar{(int32_t)(now / MINUTES_PER_DAY), p, p, p, p, 0};
        bk.tradedToday = true;
    }
    bk.today.high = std::max(bk.today.high, p);
    bk.today.low = std::min(bk.today.low, p);
    bk.today.close = p;
    bk.today.volume += (float)qty;
    bk.last = price;
    if (corpTrade) bk.lastCorpTradeDay = (int32_t)(now / MINUTES_PER_DAY);
}

// The resting order `o` trades `qty` with `taker` (NONE = station population).
void World::fill(Id station, int it, Order& o, Id taker, int64_t qty) {
    Station& st = stations[station];
    Money gross = o.price * qty;
    Id seller = o.buy ? taker : o.corp;
    Id buyer = o.buy ? o.corp : taker;
    const std::string& nm = item(it).name;
    if (seller == NONE && buyer != NONE) dbgMakerReceived[it] += toCr(gross);
    if (buyer == NONE && seller != NONE) dbgMakerPaid[it] += toCr(gross);
    if (seller == NONE) {
        st.stock[it] = (float)std::max(0.0, (double)st.stock[it] - (double)qty);
    } else {
        Money tax = (Money)std::llround(gross * salesTax(seller, station));
        pay(seller, gross, Cat::MarketSale, "Sold " + fmtInt(qty) + " " + nm + " @ " + fmtMoney(o.price, false));
        if (tax > 0) {
            pay(seller, -tax, Cat::SalesTax, st.owner != NONE ? "Market tax" : "Sales tax");
            if (st.owner != NONE) {
                pay(st.owner, tax, Cat::Taxes, "Market tax at " + st.name);
                st.taxCollected += tax;
            }
        }
        if (seller == player) pstats.sales++;
    }
    if (buyer == NONE) {
        st.stock[it] += (float)qty;
    } else {
        if (!o.buy) pay(buyer, -gross, Cat::MarketPurchase, "Bought " + fmtInt(qty) + " " + nm + " @ " + fmtMoney(o.price, false));
        addItem(buyer, station, it, qty);
    }
    if (o.corp == player)
        addLog(Chan::Market, Sev::Good, true, st.system,
               std::string(o.buy ? "Buy" : "Sell") + " order filled: " + fmtInt(qty) + " " + nm + " at " + st.name + " for " +
                   fmtMoneyShort(gross));
    o.qty -= qty;
    recordTrade(st.book(it), o.price, qty, true);
}

static void ensureLive(World& w, Id station) {
    if (!w.stations[station].makerLive) w.markActive(w.stations[station].system);
    else w.stations[station].activeUntil = std::max(w.stations[station].activeUntil, w.now + 6 * MINUTES_PER_HOUR);
}

int64_t World::instantBuy(Id corp, Id station, int it, int64_t qty, Money maxPrice, Money* spent) {
    if (!stationUsable(station) || embargoed(station, it)) { if (spent) *spent = 0; return 0; }
    ensureLive(*this, station);
    Book& bk = stations[station].book(it);
    int64_t got = 0;
    Money cost = 0;
    for (auto& o : bk.sells) {
        if (got >= qty) break;
        if (o.price > maxPrice) break;
        if (o.corp == corp || o.qty <= 0) continue;
        int64_t afford = corps[corp].wallet / std::max<Money>(1, o.price);
        int64_t n = std::min({o.qty, qty - got, afford});
        if (n <= 0) break;
        cost += o.price * n;
        fill(station, it, o, corp, n);
        got += n;
    }
    pruneBook(bk);
    if (spent) *spent = cost;
    return got;
}

int64_t World::instantSell(Id corp, Id station, int it, int64_t qty, Money minPrice, Money* got) {
    if (!stationUsable(station) || embargoed(station, it)) { if (got) *got = 0; return 0; }
    ensureLive(*this, station);
    Book& bk = stations[station].book(it);
    int64_t sold = 0;
    Money value = 0;
    for (auto& o : bk.buys) {
        if (sold >= qty) break;
        if (o.price < minPrice) break;
        if (o.corp == corp || o.qty <= 0) continue;
        int64_t n = std::min({o.qty, qty - sold, have(corp, station, it)});
        if (n <= 0) break;
        if (!takeItem(corp, station, it, n)) break;
        value += o.price * n;
        fill(station, it, o, corp, n);
        sold += n;
    }
    pruneBook(bk);
    if (got) *got = value;
    return sold;
}

Result World::placeOrder(Id corp, Id station, int it, bool buy, Money price, int64_t qty) {
    if (price <= 0 || qty <= 0) return Result::fail("Enter a price and quantity above zero.");
    if (it < 0 || it >= ITEM_COUNT) return Result::fail("Unknown item.");
    if (!stationUsable(station)) return Result::fail("That station has no working market.");
    if (embargoed(station, it)) return Result::fail("The Charter has embargoed " + item(it).name + " in Charter space.");
    Corp& c = corps[corp];
    double fee = brokerFee(corp);
    int64_t done = 0;
    if (buy) {
        Money need = (Money)(price * qty * (1.0 + fee));
        if (c.wallet < need) return Result::fail("Not enough credits: the order needs " + fmtMoney(need) + " including the broker fee.");
        done = instantBuy(corp, station, it, qty, price);
    } else {
        if (have(corp, station, it) < qty) return Result::fail("You only have " + fmtInt(have(corp, station, it)) + " here.");
        done = instantSell(corp, station, it, qty, price);
    }
    int64_t rest = qty - done;
    if (rest > 0) {
        Money feeAmt = (Money)std::llround(price * rest * fee);
        if (buy) {
            if (c.wallet < price * rest + feeAmt) rest = (c.wallet - feeAmt) / price;
            if (rest <= 0) return Result::good("Bought " + fmtInt(done) + " immediately.", done);
            pay(corp, -price * rest, Cat::MarketPurchase, "Buy order escrow: " + fmtInt(rest) + " " + item(it).name);
        } else {
            takeItem(corp, station, it, rest);
        }
        pay(corp, -feeAmt, Cat::BrokerFee, "Broker fee");
        Order o;
        o.id = nextOrderId++;
        o.corp = corp;
        o.item = it;
        o.buy = buy;
        o.price = price;
        o.qty = o.qty0 = rest;
        o.placed = now;
        o.expires = now + 30 * MINUTES_PER_DAY;
        Book& bk = stations[station].book(it);
        (buy ? bk.buys : bk.sells).push_back(o);
        sortBook(bk);
        c.orderSpots.insert(hkey(station, it));
        if (corp == player) pstats.ordersPlaced++;
        Result r = Result::good(done ? "Filled " + fmtInt(done) + " now; " + fmtInt(rest) + " left on the market."
                                     : "Order placed for " + fmtInt(rest) + ".", done);
        r.id = (Id)o.id;
        return r;
    }
    return Result::good("Filled all " + fmtInt(done) + " immediately.", done);
}

static bool cancelIn(World& w, Station& st, Book& bk, int64_t orderId, Id corp) {
    for (int side = 0; side < 2; side++) {
        auto& v = side ? bk.buys : bk.sells;
        for (size_t k = 0; k < v.size(); k++) {
            Order& o = v[k];
            if (o.id != orderId) continue;
            if (corp != NONE && o.corp != corp) return false;
            if (o.corp != NONE) {
                if (o.buy) w.pay(o.corp, o.price * o.qty, Cat::MarketPurchase, "Buy order refund: " + item(bk.item).name);
                else w.addItem(o.corp, st.id, bk.item, o.qty);
            }
            v.erase(v.begin() + (long)k);
            return true;
        }
    }
    return false;
}

bool World::cancelOrder(int64_t orderId, Id corp) {
    if (corp != NONE) {
        for (int64_t spot : corps[corp].orderSpots) {
            Station& st = stations[(Id)(spot / HANGAR_KEY)];
            Book* bk = st.findBook((int)(spot % HANGAR_KEY));
            if (bk && cancelIn(*this, st, *bk, orderId, corp)) return true;
        }
        return false;
    }
    for (auto& st : stations)
        for (auto& bk : st.books)
            if (cancelIn(*this, st, bk, orderId, corp)) return true;
    return false;
}

void World::expireOrders() {
    for (auto& c : corps) {
        if (c.orderSpots.empty()) continue;
        std::vector<int64_t> dead;
        for (auto it = c.orderSpots.begin(); it != c.orderSpots.end();) {
            Station& st = stations[(Id)(*it / HANGAR_KEY)];
            Book* bk = st.findBook((int)(*it % HANGAR_KEY));
            bool any = false;
            if (bk) {
                for (auto& o : bk->sells)
                    if (o.corp == c.id) { any = true; if (o.expires <= now || st.dead) dead.push_back(o.id); }
                for (auto& o : bk->buys)
                    if (o.corp == c.id) { any = true; if (o.expires <= now || st.dead) dead.push_back(o.id); }
            }
            if (!any) it = c.orderSpots.erase(it); else ++it;
        }
        for (auto id : dead) {
            cancelOrder(id, c.id);
            if (c.player) addLog(Chan::Market, Sev::Info, true, NONE, "A market order expired and was returned to you.");
        }
    }
}

// ---------------------------------------------------------- market makers ----
void World::stationEconomy(Station& st, double hours) {
    if (!popTrades(st) || hours <= 0) return;
    hours = std::min(hours, 24.0 * 7);
    for (int i = 0; i < BPO_FIRST; i++) {
        double T = st.target[i];
        if (T <= 0) continue;
        Elastic el = elastic(i);
        double S = st.stock[i];
        double prodEff = st.prod[i] * st.prodMul * std::clamp(2.5 - S / T, 0.0, 1.0);
        double consEff = S > 0 ? st.cons[i] * st.eventMul[i] : 0.0;
        S += (prodEff - consEff) * hours / 24.0;
        S += (T - S) * (1.0 - std::pow(1.0 - el.revert / 24.0, hours));
        st.stock[i] = (float)std::clamp(S, 0.0, T * 4.0);
    }
    for (int i = BPC_FIRST; i < ITEM_COUNT; i++) {
        double T = st.target[i];
        if (T <= 0) continue;
        double S = st.stock[i] + (st.prod[i] * std::clamp(2.5 - st.stock[i] / T, 0.0, 1.0) - st.cons[i]) * hours / 24.0;
        S += (T - S) * (1.0 - std::pow(1.0 - 0.3 / 24.0, hours));
        st.stock[i] = (float)std::clamp(S, 0.0, T * 4.0);
    }
}

void World::makerRepost(Station& st) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (st.target[i] <= 0 && st.bookOf[i] < 0) continue;
        Book& bk = st.book(i);
        auto isMaker = [](const Order& o) { return o.corp == NONE; };
        bk.sells.erase(std::remove_if(bk.sells.begin(), bk.sells.end(), isMaker), bk.sells.end());
        bk.buys.erase(std::remove_if(bk.buys.begin(), bk.buys.end(), isMaker), bk.buys.end());
        makerLadder(*this, st, i, &bk.sells, &bk.buys, &nextOrderId, now);
        sortBook(bk);
    }
    st.makerLive = popTrades(st);
}

// The population also takes good deals from corporations' resting orders.
void World::makerSweep(Station& st) {
    if (!popTrades(st)) return;
    for (int i = 0; i < ITEM_COUNT; i++) {
        double T = st.target[i];
        if (T <= 0 || isBpo(i)) continue;
        Book* bkp = st.findBook(i);
        if (!bkp) continue;
        Book& bk = *bkp;
        Money P = refPrice(st.id, i);
        bool hub = st.type == StationType::TradeHub;
        bool consumer = st.cons[i] > 0;
        if (consumer || hub) {
            double want = std::max(0.0, 1.6 * T - st.stock[i]);
            for (auto& o : bk.sells) {
                if (want < 1) break;
                if (o.corp == NONE || o.qty <= 0) continue;
                if (o.price > P * 97 / 100) break;
                int64_t n = std::min<int64_t>(o.qty, (int64_t)want);
                if (n <= 0) break;
                fill(st.id, i, o, NONE, n);
                want -= (double)n;
            }
        }
        double reserve = hub ? 0.2 * T : consumer ? 0.35 * T : 0.0;
        double sellable = std::max(0.0, (double)st.stock[i] - reserve);
        for (auto& o : bk.buys) {
            if (sellable < 1) break;
            if (o.corp == NONE || o.qty <= 0) continue;
            if (o.price < P * 103 / 100) break;
            int64_t n = std::min<int64_t>(o.qty, (int64_t)sellable);
            if (n <= 0) break;
            fill(st.id, i, o, NONE, n);
            sellable -= (double)n;
        }
        pruneBook(bk);
    }
}

}  // namespace nb
