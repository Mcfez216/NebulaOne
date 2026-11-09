// NebulaOne - Finance tabs for the capital markets: futures, bonds, short
// selling and the underwriting desk; and the Boardroom's takeover defences.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

static const char* ratingName(int r) {
    static const char* L[] = {"A", "B", "C", "D", "E"};
    return L[std::clamp(r, 0, 4)];
}
static std::string pct(double v, int dp = 2) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f%%", dp, v * 100.0);
    return b;
}
static int daysTo(const World& w, Tick t) { return std::max(0, (int)std::ceil((double)(t - w.now) / MINUTES_PER_DAY)); }

// --------------------------------------------------------------- futures --
void finFutures(App& app) {
    World& w = app.world;
    static Id hub = NONE;
    static int it = MINERAL_FIRST;
    static int term = 14;
    static int64_t qty = 10000;
    if (hub == NONE || std::find(w.hubs.begin(), w.hubs.end(), hub) == w.hubs.end()) hub = w.hubs.empty() ? NONE : w.hubs[0];
    if (hub == NONE) { textDim("No Charter hub is trading."); return; }
    ImGui::PushTextWrapPos(0);
    textDim("Cash-settled contracts on the Charter hubs. A long profits if the hub's price is higher at expiry, a short if it "
            "is lower. You post 15%% margin; if losses eat it down to 8%% you are called for more or closed out.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Hub", w.stations[hub].name.c_str())) {
        for (Id h : w.hubs) {
            ImGui::PushID(h);
            if (ImGui::Selectable(w.stations[h].name.c_str(), h == hub)) hub = h;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Commodity", item(it).name.c_str())) {
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (!futuresTradeable(i)) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(item(i).name.c_str(), i == it)) it = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    Money spot = w.refPrice(hub, it);
    ImGui::BeginGroup();
    if (beginTable("fq", 4, 0, ImVec2(420, 5 * ImGui::GetFrameHeightWithSpacing() + 6))) {
        ImGui::TableSetupColumn("Delivery", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("You buy at");
        ImGui::TableSetupColumn("You sell at");
        ImGui::TableSetupColumn("vs spot");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Spot (today)");
        ImGui::TableNextColumn(); cellMoney(spot);
        ImGui::TableNextColumn(); cellMoney(spot);
        ImGui::TableNextColumn(); rightText("-", col::Dim);
        for (int d : FUTURE_TERMS) {
            Money f = w.futuresPrice(hub, it, d);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(d);
            if (ImGui::Selectable((std::to_string(d) + " days").c_str(), term == d, ImGuiSelectableFlags_SpanAllColumns)) term = d;
            ImGui::PopID();
            ImGui::TableNextColumn(); cellMoney((Money)std::llround(f * 1.005));
            ImGui::TableNextColumn(); cellMoney((Money)std::llround(f * 0.995));
            double v = spot > 0 ? (double)f / spot - 1.0 : 0;
            ImGui::TableNextColumn(); rightText((v >= 0 ? "+" : "") + pct(v, 1), v >= 0 ? col::Good : col::Bad);
        }
        ImGui::EndTable();
    }
    inputQty("Quantity", qty, 140);
    Money f = w.futuresPrice(hub, it, term);
    Money notional = f * qty;
    textDim("%s notional. Margin %s, fee %s.", fmtMoneyShort(notional).c_str(), fmtMoneyShort(notional * 15 / 100).c_str(),
            fmtMoneyShort(notional / 500).c_str());
    if (accentButton(("Buy " + std::to_string(term) + "-day (long)").c_str(), ImVec2(170, 0))) {
        Command c; c.type = Cmd::OpenFuture; c.a = hub; c.item = it; c.qty = qty; c.flag = true; c.b = term;
        app.run(c);
    }
    ImGui::SameLine();
    if (ImGui::Button(("Sell " + std::to_string(term) + "-day (short)").c_str(), ImVec2(170, 0))) {
        Command c; c.type = Cmd::OpenFuture; c.a = hub; c.item = it; c.qty = qty; c.flag = false; c.b = term;
        app.run(c);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    const Book* bk = w.stations[hub].findBook(it);
    if (bk && bk->hist.size() > 2 && ImPlot::BeginPlot("##fh", ImVec2(-1, 190), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
        std::vector<double> x, y;
        int n = std::min<int>(60, (int)bk->hist.size());
        for (int i = (int)bk->hist.size() - n; i < (int)bk->hist.size(); i++) {
            x.push_back((double)(i - (int)bk->hist.size()));
            y.push_back(bk->hist[i].close);
        }
        double fx[2] = {0, (double)term}, fy[2] = {toCr(spot), toCr(f)};
        double lo = std::min(fy[0], fy[1]), hi = std::max(fy[0], fy[1]);
        for (double v : y) if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        double pad = std::max(0.01, (hi - lo) * 0.08);
        ImPlot::SetupAxes("days", "cr");
        ImPlot::SetupAxesLimits(x.empty() ? -1 : x.front(), (double)term + 1, lo - pad, hi + pad, ImPlotCond_Always);
        ImPlot::PlotLine("close", x.data(), y.data(), (int)x.size(), {ImPlotProp_LineColor, col::v4(col::Accent)});
        ImPlot::PlotLine("forward", fx, fy, 2, {ImPlotProp_LineColor, col::v4(col::Warn)});
        ImPlot::EndPlot();
    } else {
        textDim("No price history at this hub yet.");
    }
    header("Your contracts");
    int n = 0;
    if (beginTable("fpos", 10)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Commodity", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hub");
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Quantity");
        ImGui::TableSetupColumn("Entry");
        ImGui::TableSetupColumn("Mark");
        ImGui::TableSetupColumn("Margin");
        ImGui::TableSetupColumn("Open P/L");
        ImGui::TableSetupColumn("Expires");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int i = (int)w.futures.size() - 1; i >= 0; i--) {
            const Future& x = w.futures[i];
            if (x.corp != w.player || (!x.open && w.now - x.expiry > 10 * MINUTES_PER_DAY && w.now - x.opened > 10 * MINUTES_PER_DAY)) continue;
            n++;
            Money mark = x.open ? w.futuresPrice(x.hub, x.item, daysTo(w, x.expiry)) : 0;
            Money pnl = x.open ? (mark - x.price) * x.qty * (x.isLong ? 1 : -1) : x.result;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); textColored(x.open ? col::Text : col::Dim, "%s", item(x.item).name.c_str());
            ImGui::TableNextColumn(); textDim("%s", w.stations[x.hub].name.c_str());
            ImGui::TableNextColumn(); textColored(x.isLong ? col::Good : col::Warn, x.isLong ? "Long" : "Short");
            ImGui::TableNextColumn(); cellInt(x.qty);
            ImGui::TableNextColumn(); cellMoney(x.price);
            ImGui::TableNextColumn(); if (x.open) cellMoney(mark); else rightText("-", col::Dim);
            ImGui::TableNextColumn(); if (x.open) rightText(fmtMoneyShort(x.margin, false)); else rightText("-", col::Dim);
            ImGui::TableNextColumn(); rightText(fmtMoneyShort(pnl, false), pnl >= 0 ? col::Good : col::Bad);
            ImGui::TableNextColumn();
            if (x.open) textDim("%d d", daysTo(w, x.expiry)); else textDim("settled");
            ImGui::TableNextColumn();
            ImGui::PushID(x.id);
            if (x.open && ImGui::SmallButton("Close")) { Command c; c.type = Cmd::CloseFuture; c.a = x.id; app.run(c); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!n) textDim("No contracts. Buying the hub dry before your long expires is a squeeze; it works, and it costs.");
}

// ----------------------------------------------------------------- bonds --
void finBonds(App& app) {
    World& w = app.world;
    Corp& me = w.pc();
    if (ImGui::BeginTable("bk", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Credit rating", ratingName(me.rating), me.rating <= 1 ? col::Good : me.rating >= 3 ? col::Warn : col::Text);
        ImGui::TableNextColumn(); kpi("30-day coupon", pct(w.bondRate(w.player, 30)));
        ImGui::TableNextColumn(); kpi("Bonds outstanding", fmtMoneyShort(w.bondsIssued(w.player)));
        ImGui::TableNextColumn(); kpi("Room to issue", fmtMoneyShort(w.bondCapacity(w.player)));
        ImGui::EndTable();
    }
    header("Issue bonds");
    ImGui::PushTextWrapPos(0);
    textDim("Bonds are 1M cr each, sold to the public at par less a 1%% fee. You pay a coupon every 30 days and the whole principal "
            "at maturity: cheaper than a bank loan and nothing to repay until the end, which suits a long project. Miss a payment "
            "for six days and you default.");
    ImGui::PopTextWrapPos();
    static int64_t units = 10;
    static int term = 90;
    inputQty("Bonds (1M cr each)", units, 120);
    for (int d : BOND_TERMS) {
        ImGui::SameLine();
        ImGui::RadioButton((std::to_string(d) + " d").c_str(), &term, d);
    }
    double rate = w.bondRate(w.player, term);
    Money face = units * BOND_UNIT;
    textDim("At %s per 30 days: %s every 30 days, %s back on day %d. Total interest %s.", pct(rate).c_str(),
            fmtMoneyShort((Money)std::llround(face * rate)).c_str(), fmtMoneyShort(face).c_str(), term,
            fmtMoneyShort((Money)std::llround(face * rate * term / 30.0)).c_str());
    if (accentButton("Issue", ImVec2(120, 0))) { Command c; c.type = Cmd::IssueBond; c.qty = units; c.b = term; app.run(c); }
    // your issues
    int mine = 0;
    for (auto& b : w.bonds) mine += b.issuer == w.player && (!b.done || w.now - b.maturity < 30 * MINUTES_PER_DAY);
    if (mine) {
        header("Your bonds");
        if (beginTable("mybonds", 7, 0, ImVec2(0, std::min(5, mine + 1) * ImGui::GetFrameHeightWithSpacing() + 6))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Issued");
            ImGui::TableSetupColumn("Face", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Coupon / 30d");
            ImGui::TableSetupColumn("Next coupon");
            ImGui::TableSetupColumn("Matures");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (auto& b : w.bonds) {
                if (b.issuer != w.player || (b.done && w.now - b.maturity >= 30 * MINUTES_PER_DAY)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); textDim("%s", fmtDate(b.issued).substr(0, 10).c_str());
                ImGui::TableNextColumn(); rightText(fmtMoneyShort(b.units * BOND_UNIT, false));
                ImGui::TableNextColumn(); rightText(pct(b.rate30));
                Tick next = b.issued + (Tick)(b.coupons + 1) * 30 * MINUTES_PER_DAY;
                ImGui::TableNextColumn(); if (!b.done) textDim("%d d", daysTo(w, next)); else textDim("-");
                ImGui::TableNextColumn(); if (!b.done) textDim("%d d", daysTo(w, b.maturity)); else textDim("-");
                ImGui::TableNextColumn();
                if (b.defaulted) textColored(col::Bad, "Defaulted");
                else if (b.done) textColored(col::Good, "Repaid");
                else if (b.missed) textColored(col::Bad, "%d days overdue", b.missed);
                else textDim("Current");
                ImGui::TableNextColumn();
                ImGui::PushID(b.id);
                if (!b.done && ImGui::SmallButton("Call at 102")) { Command c; c.type = Cmd::BuyBond; c.a = b.id; c.qty = b.heldBy(NONE); app.run(c); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Buy the whole issue back from the public now, at a 2%% premium.");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    header("Rivals' bonds on offer");
    static int64_t buyN = 2;
    inputQty("Bonds to buy", buyN, 100);
    int offers = 0;
    if (beginTable("offers", 8, 0, ImVec2(0, 160))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Issuer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Profile");
        ImGui::TableSetupColumn("Rating");
        ImGui::TableSetupColumn("On offer");
        ImGui::TableSetupColumn("Coupon / 30d");
        ImGui::TableSetupColumn("Term");
        ImGui::TableSetupColumn("Closes");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (auto& b : w.bonds) {
            if (b.done || b.issuer == w.player || b.onOffer <= 0 || b.offerEnds <= w.now) continue;
            offers++;
            const Corp& is = w.corps[b.issuer];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(is.name.c_str());
            ImGui::TableNextColumn(); textColored(is.arch == Arch::Pirate || is.arch == Arch::Ganker ? col::Bad : col::Dim, "%s", archName(is.arch));
            ImGui::TableNextColumn(); textColored(is.rating <= 1 ? col::Good : is.rating >= 3 ? col::Bad : col::Text, "%s", ratingName(is.rating));
            ImGui::TableNextColumn(); rightText(fmtMoneyShort(b.onOffer * BOND_UNIT, false));
            ImGui::TableNextColumn(); rightText(pct(b.rate30), col::Good);
            ImGui::TableNextColumn(); rightText(std::to_string((int)((b.maturity - b.issued) / MINUTES_PER_DAY)) + " d");
            ImGui::TableNextColumn(); textDim("%s", fmtDuration(b.offerEnds - w.now).c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(b.id);
            if (ImGui::SmallButton("Buy")) { Command c; c.type = Cmd::BuyBond; c.a = b.id; c.qty = std::min(buyN, b.onOffer); app.run(c); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!offers) textDim("Nothing on offer. Rivals come to the market every few days.");
    int held = 0;
    for (auto& b : w.bonds) held += !b.done && b.heldBy(w.player) > 0;
    if (held) {
        header("Bonds you hold");
        if (beginTable("held", 7, 0, ImVec2(0, std::min(6, held + 1) * ImGui::GetFrameHeightWithSpacing() + 6))) {
            ImGui::TableSetupColumn("Issuer", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Rating");
            ImGui::TableSetupColumn("Face");
            ImGui::TableSetupColumn("Coupon / 30d");
            ImGui::TableSetupColumn("Matures");
            ImGui::TableSetupColumn("Bid");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (auto& b : w.bonds) {
                int64_t h = b.done ? 0 : b.heldBy(w.player);
                if (!h) continue;
                const Corp& is = w.corps[b.issuer];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(is.name.c_str());
                ImGui::TableNextColumn(); textColored(is.rating <= 1 ? col::Good : is.rating >= 3 ? col::Bad : col::Text, "%s", ratingName(is.rating));
                ImGui::TableNextColumn(); rightText(fmtMoneyShort(h * BOND_UNIT, false));
                ImGui::TableNextColumn(); rightText(pct(b.rate30));
                ImGui::TableNextColumn(); textDim("%d d", daysTo(w, b.maturity));
                ImGui::TableNextColumn(); rightText(pct(0.985 - 0.02 * std::clamp(is.rating, 0, 4), 1), col::Dim);
                ImGui::TableNextColumn();
                ImGui::PushID(b.id);
                if (ImGui::SmallButton("Sell")) { Command c; c.type = Cmd::SellBond; c.a = b.id; c.qty = h; app.run(c); }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

// ---------------------------------------------------------------- shorts --
void finShorts(App& app) {
    World& w = app.world;
    static Id pick = NONE;
    static int64_t qty = 10000;
    ImGui::PushTextWrapPos(0);
    textDim("Borrow a rival's shares from the Exchange and sell them; buy them back later to return. You profit if the price "
            "falls. Margin is 50%% of the sale, called at 25%%; the loan costs 0.05%% a day and you pay any dividend. Bad news "
            "about a rival, or a smear campaign by your Intelligence Director, is what you are betting on.");
    ImGui::PopTextWrapPos();
    std::vector<Id> ids;
    for (auto& c : w.corps)
        if (!c.player && c.alive) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
        return (double)w.corps[a].sharePrice * w.corps[a].sharesOut > (double)w.corps[b].sharePrice * w.corps[b].sharesOut;
    });
    std::vector<int64_t> interest(w.corps.size(), 0);
    for (auto& s : w.shorts)
        if (s.open) interest[s.target] += s.qty;
    if (beginTable("shortable", 7, 0, ImVec2(0, 200))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Corporation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ticker");
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("30 days");
        ImGui::TableSetupColumn("Lendable");
        ImGui::TableSetupColumn("You are short");
        ImGui::TableSetupColumn("Defences");
        ImGui::TableHeadersRow();
        for (Id id : ids) {
            const Corp& c = w.corps[id];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(id);
            if (ImGui::Selectable(c.name.c_str(), pick == id, ImGuiSelectableFlags_SpanAllColumns)) pick = id;
            ImGui::PopID();
            ImGui::TableNextColumn(); monoText(c.ticker);
            ImGui::TableNextColumn(); cellMoney(c.sharePrice);
            double ch = 0;
            if (c.shareHist.size() > 30) ch = (double)c.sharePrice / std::max<Money>(1, c.shareHist[c.shareHist.size() - 31]) - 1.0;
            ImGui::TableNextColumn(); rightText((ch >= 0 ? "+" : "") + pct(ch, 1), ch >= 0 ? col::Good : col::Bad);
            ImGui::TableNextColumn(); cellInt(w.lendableShares(id));
            ImGui::TableNextColumn(); if (interest[id]) cellInt(interest[id]); else rightText("-", col::Dim);
            ImGui::TableNextColumn();
            if (c.poisonPill) textColored(col::Warn, "pill");
            else if (c.defenceUsed) textDim("used");
            else textDim("-");
            if (c.smear > 0.02f) { ImGui::SameLine(); textColored(col::Bad, "smeared"); }
        }
        ImGui::EndTable();
    }
    if (pick != NONE && w.corps[pick].alive) {
        const Corp& c = w.corps[pick];
        inputQty("Shares", qty, 120);
        ImGui::SameLine();
        Money proceeds = c.sharePrice * qty;
        textDim("Sells for about %s; margin %s.", fmtMoneyShort(proceeds).c_str(), fmtMoneyShort(proceeds / 2).c_str());
        ImGui::SameLine();
        if (accentButton(("Sell " + c.ticker + " short").c_str())) { Command cm; cm.type = Cmd::OpenShort; cm.a = pick; cm.qty = qty; app.run(cm); }
    } else {
        textDim("Pick a corporation to short.");
    }
    header("Open positions");
    int n = 0;
    if (beginTable("spos", 8)) {
        ImGui::TableSetupColumn("Corporation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shares");
        ImGui::TableSetupColumn("Sold at");
        ImGui::TableSetupColumn("Price now");
        ImGui::TableSetupColumn("Margin");
        ImGui::TableSetupColumn("P/L");
        ImGui::TableSetupColumn("Loan fee / day");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int i = (int)w.shorts.size() - 1; i >= 0; i--) {
            const ShortPos& s = w.shorts[i];
            if (s.corp != w.player || (!s.open && w.now - s.opened > 20 * MINUTES_PER_DAY)) continue;
            n++;
            const Corp& t = w.corps[s.target];
            Money pnl = s.open ? (s.price - t.sharePrice) * s.qty : s.result;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); textColored(s.open ? col::Text : col::Dim, "%s [%s]", t.name.c_str(), t.ticker.c_str());
            ImGui::TableNextColumn(); cellInt(s.qty);
            ImGui::TableNextColumn(); cellMoney(s.price);
            ImGui::TableNextColumn(); if (s.open) cellMoney(t.sharePrice); else rightText("covered", col::Dim, false);
            ImGui::TableNextColumn(); if (s.open) rightText(fmtMoneyShort(s.margin, false)); else rightText("-", col::Dim);
            ImGui::TableNextColumn(); rightText(fmtMoneyShort(pnl, false), pnl >= 0 ? col::Good : col::Bad);
            ImGui::TableNextColumn(); if (s.open) cellMoney(t.sharePrice * s.qty / 2000); else rightText("-", col::Dim);
            ImGui::TableNextColumn();
            ImGui::PushID(s.id);
            if (s.open && ImGui::SmallButton("Cover")) { Command c; c.type = Cmd::CloseShort; c.a = s.id; app.run(c); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!n) textDim("No short positions.");
}

// ---------------------------------------------------------- underwriting --
void finUnderwriting(App& app) {
    World& w = app.world;
    Underwriting& u = w.uw;
    static Underwriting form;
    static bool init = false;
    if (!init || (!u.open && form.capacity == 0)) {
        form = u;
        if (form.capacity == 0) form.capacity = 100000000 * CR;
        init = true;
    }
    ImGui::PushTextWrapPos(0);
    textDim("Write hull cover for other corporations. They pay you a premium for 30 days of cover and you pay 70%% of the hull if "
            "the ship is lost. Price under the Charter underwriters and more of them buy; take on dangerous work and the claims "
            "come in. A Charter licence costs 5M cr; you may write cover up to four times your cash, and stop writing when cash "
            "falls under a tenth of the cover in force.");
    ImGui::PopTextWrapPos();
    if (ImGui::BeginTable("uk", 5, ImGuiTableFlags_SizingStretchSame)) {
        Money net = u.premiums - u.claims;
        ImGui::TableNextColumn(); kpi("Desk", u.open ? "Writing" : u.licensed ? "Run-off" : "Not licensed", u.open ? col::Good : col::Dim);
        ImGui::TableNextColumn(); kpi("Cover in force", fmtMoneyShort(u.exposure(), false) + " / " + fmtMoneyShort(u.capacity, false));
        ImGui::TableNextColumn(); kpi("Premiums written", fmtMoneyShort(u.premiums, false), col::Good);
        ImGui::TableNextColumn(); kpi("Claims paid", fmtMoneyShort(u.claims, false) + " (" + std::to_string(u.paidClaims) + ")", u.claims > 0 ? col::Warn : col::Text);
        ImGui::TableNextColumn();
        kpi("Result", fmtMoneyShort(net, false), net >= 0 ? col::Good : col::Bad);
        ImGui::EndTable();
    }
    if (u.premiums > 0) textDim("Loss ratio %.0f%%: claims as a share of premiums. Under 100%% the desk makes money.", 100.0 * u.claims / u.premiums);
    header("Terms");
    ImGui::SetNextItemWidth(240);
    ImGui::SliderFloat("Price against the Charter rate", &form.rateMul, 0.6f, 1.6f, "%.2fx");
    ImGui::SetNextItemWidth(240);
    ImGui::SliderInt("Cover ships working at risk up to", &form.maxRisk, 5, 100);
    inputCredits("Most cover in force", form.capacity, 180);
    double demand = std::clamp((1.7 - form.rateMul) / 0.7, 0.05, 1.5);
    textDim("At %.2fx about %.0f%% of eligible ships a month take your cover.", form.rateMul,
            100.0 * (1.0 - std::pow(1.0 - 0.03 * demand, 30)));
    if (accentButton(u.open ? "Update terms" : u.licensed ? "Reopen the desk" : "Buy licence and open (5M cr)", ImVec2(230, 0))) {
        Command c; c.type = Cmd::SetUnderwriting; c.uw = form; c.uw.open = true;
        app.run(c);
    }
    if (u.open) {
        ImGui::SameLine();
        if (ImGui::Button("Stop writing new cover")) { Command c; c.type = Cmd::SetUnderwriting; c.uw.open = false; app.run(c); }
    }
    if (!u.book.empty()) {
        header("The book");
        std::vector<std::pair<Id, std::pair<Money, Tick>>> rows(u.book.begin(), u.book.end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
        if (beginTable("book", 5)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Ship", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Owner");
            ImGui::TableSetupColumn("Working at");
            ImGui::TableSetupColumn("Cover");
            ImGui::TableSetupColumn("Expires");
            ImGui::TableHeadersRow();
            int shown = 0;
            for (auto& [sid, cv] : rows) {
                if (++shown > 200) break;
                const Ship& s = w.ships[sid];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(shipDef(s.def).name.c_str());
                ImGui::TableNextColumn(); textDim("%s", w.corps[s.corp].name.c_str());
                ImGui::TableNextColumn();
                float r = (float)w.shipRisk(s);
                textColored(r > form.maxRisk ? col::Bad : r > 40 ? col::Warn : col::Text, "risk %d", (int)r);
                ImGui::TableNextColumn(); rightText(fmtMoneyShort(cv.first, false));
                ImGui::TableNextColumn(); textDim("%s", fmtDuration(cv.second - w.now).c_str());
            }
            ImGui::EndTable();
        }
    }
}

// ------------------------------------------------- the Boardroom's defences --
void boardDefences(App& app) {
    World& w = app.world;
    Corp& c = w.pc();
    header("Takeover defence");
    if (c.poisonPill) textColored(col::Good, "Poison pill armed: a raider passing 25%% of your shares is diluted by half.");
    else {
        textDim("No poison pill.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Adopt a poison pill (2M cr, board -4)")) { Command cm; cm.type = Cmd::AdoptPill; app.run(cm); }
    }
    if (c.raider == NONE || !w.corps[c.raider].alive) {
        textDim("No one is building a stake in you.");
        return;
    }
    const Corp& r = w.corps[c.raider];
    auto it = r.shares.find(w.player);
    int64_t stake = it == r.shares.end() ? 0 : it->second;
    if (c.raidDeadline == 0) {
        textDim("Unusual buying in your shares: someone is accumulating (%.1f%% so far).", 100.0 * stake / c.sharesOut);
        return;
    }
    textColored(col::Bad, "%s holds %.1f%% of you and wants paying off. It walks away and dumps the shares in %s.", r.name.c_str(),
                100.0 * stake / c.sharesOut, fmtDuration(std::max<Tick>(0, c.raidDeadline - w.now)).c_str());
    textDim("Until then it lobbies your board: confidence falls a little every day.");
    Money gm = (Money)std::llround((double)c.sharePrice * stake * 1.25);
    if (ImGui::Button(("Pay greenmail: " + fmtMoneyShort(gm)).c_str())) { Command cm; cm.type = Cmd::PayGreenmail; app.run(cm); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Buy its stake back at a 25%% premium. The shares are cancelled. Board -6.");
    static Id knight = NONE;
    ImGui::SetNextItemWidth(240);
    const char* prev = knight == NONE ? "Pick a friendly corporation" : w.corps[knight].name.c_str();
    if (ImGui::BeginCombo("##knight", prev)) {
        for (auto& k : w.corps) {
            if (!k.alive || k.player || k.id == c.raider) continue;
            float st = w.standing(k.id, w.player);
            if (st < 1.0f) continue;
            ImGui::PushID(k.id);
            char lbl[160];
            std::snprintf(lbl, sizeof lbl, "%s  (standing %+.1f, %s)", k.name.c_str(), st, fmtMoneyShort(k.wallet).c_str());
            if (ImGui::Selectable(lbl, knight == k.id)) knight = k.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(knight == NONE);
    if (ImGui::Button("Ask for a white knight")) { Command cm; cm.type = Cmd::WhiteKnight; cm.a = knight; app.run(cm); }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A friend (standing 1.0 or better) buys the rest of your public float so the raider cannot. Costs standing.");
}

}  // namespace nb
