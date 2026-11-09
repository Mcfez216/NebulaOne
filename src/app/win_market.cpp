// NebulaOne - the Market: order books, history, the all-stations spreadsheet.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

// Walk one side of a book: how much of `qty` fills within `limit`, and for how much.
static std::pair<int64_t, Money> walk(const std::vector<Order>& side, int64_t qty, Money limit, bool buying, Id me) {
    int64_t got = 0;
    Money cost = 0;
    for (auto& o : side) {
        if (got >= qty) break;
        if (o.corp == me) continue;
        if (buying ? o.price > limit : o.price < limit) break;
        int64_t n = std::min(o.qty, qty - got);
        got += n;
        cost += o.price * n;
    }
    return {got, cost};
}

static void bookTable(App& a, const char* id, const std::vector<Order>& side, bool buys) {
    World& w = a.world;
    if (ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuterH, ImVec2(0, 210))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(buys ? "Buyer" : "Seller", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        int n = 0;
        for (auto& o : side) {
            if (n++ >= 40) break;
            bool mine = o.corp == w.player;
            ImGui::TableNextRow();
            if (mine) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, withAlpha(col::Accent, 0.12f));
            ImGui::TableNextColumn(); rightText(fmtMoney(o.price, false), buys ? col::Good : col::Text);
            ImGui::TableNextColumn(); rightText(fmtInt(o.qty));
            ImGui::TableNextColumn();
            if (o.corp == NONE) textDim("Population");
            else textColored(mine ? col::Accent : col::Text, "%s", w.corps[o.corp].ticker.c_str());
        }
        if (side.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            textDim(buys ? "No buyers" : "Nothing for sale");
        }
        ImGui::EndTable();
    }
}

// "3h ago", "2d 4h ago"
static std::string ago(const World& w, Tick t) { return t < 0 ? "never" : w.now - t < 60 ? "just now" : fmtDuration(w.now - t) + " ago"; }

void App::winMarket() {
    if (!beginWindow(*this, W_MARKET, ImVec2(80, 64), ImVec2(1000, 640))) { ImGui::End(); return; }
    World& w = world;
    if (mktStation == NONE) mktStation = w.pc().home;
    Id home = w.pc().home;
    // fog of war: a live feed where our sensors reach, else the last snapshot we took
    const bool live = w.seen(w.stations[mktStation].system);
    const MarketSnap* snap = nullptr;
    if (!live) {
        auto itS = w.intel.find(mktStation);
        if (itS != w.intel.end()) snap = &itS->second;
    }

    // ---- left: station and item list
    ImGui::BeginChild("mleft", ImVec2(250 * settings.uiScale, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    header("Station");
    stationPicker(*this, "##mst", mktStation, -1);
    if (ImGui::SmallButton("Home")) mktStation = home;
    if (selSystem != NONE && !w.systems[selSystem].stations.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(("At " + w.systems[selSystem].name).c_str())) mktStation = w.systems[selSystem].stations[0];
    }
    header("Items");
    static char filt[48] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##if", "filter items", filt, sizeof filt);
    std::string f = upper(filt);
    ImGui::BeginChild("items");
    for (int cat = 0; cat < 9; cat++) {
        const char* cn[] = {"Ore", "Minerals", "Trade goods", "Components", "Ship modules", "Hulls", "Structures", "Blueprint originals", "Blueprint copies"};
        ImGui::SetNextItemOpen(cat < 3 || (int)item(mktItem).cat == cat, ImGuiCond_Once);
        if (!ImGui::TreeNodeEx(cn[cat], ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        for (int i = 0; i < ITEM_COUNT; i++) {
            if ((int)item(i).cat != cat) continue;
            if (!f.empty() && upper(item(i).name).find(f) == std::string::npos) continue;
            ImGui::PushID(i);
            Money ask = 0;
            if (live) ask = w.bestAsk(mktStation, i);
            else if (snap)
                if (const SnapRow* r = snap->find(i)) ask = r->ask;
            if (ImGui::Selectable(item(i).name.c_str(), mktItem == i, ImGuiSelectableFlags_None)) mktItem = i;
            if (ask) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 78);
                ImGui::PushFont(fonts.mono, 13.0f);
                textDim("%10s", fmtMoneyShort(ask, false).c_str());
                ImGui::PopFont();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: the selected market
    ImGui::BeginChild("mright");
    const ItemDef& it = item(mktItem);
    const Station& st = w.stations[mktStation];
    const StarSystem& sy = w.systems[st.system];
    static std::vector<Order> bkSells, bkBuys;
    w.bookView(mktStation, mktItem, bkSells, bkBuys);
    static const Book emptyBook;
    const Book& bk = st.findBook(mktItem) ? *st.findBook(mktItem) : emptyBook;
    ImGui::PushFont(fonts.title, 21.0f);
    ImGui::TextUnformatted(it.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    textDim("%s", catName(it.cat));
    ImGui::SameLine();
    textDim("·  %s m3 per unit  ·  anchor %s", fmtQty(it.volume).c_str(), fmtMoney(it.base).c_str());
    zoneBadge(sy.zone);
    ImGui::SameLine();
    riskBadge(sy.risk, sy.riskLive);
    ImGui::SameLine();
    ImGui::Text("%s", st.name.c_str());
    ImGui::SameLine();
    textDim("(%s, %s, %d jumps from home)", stationTypeName(st.type), sy.hexId.c_str(), w.jumps(w.stations[home].system, st.system));
    if (live && !st.makerLive) { ImGui::SameLine(); textDim("  quotes indicative until a fleet arrives"); }
    if (st.isOutpost()) { ImGui::SameLine(); textColored(col::Warn, "  outpost of %s, %.0f%% market tax", w.corps[st.owner].ticker.c_str(), st.marketTax * 100); }
    int64_t mine = w.have(w.player, mktStation, mktItem);
    textDim("You hold here: ");
    ImGui::SameLine(0, 0);
    monoText(fmtInt(mine), mine > 0 ? col::Accent : col::Dim);
    if (!it.desc.empty()) { ImGui::SameLine(); textDim("  ·  %s", it.desc.c_str()); }
    if (!live) {
        Id reg = sy.region;
        if (snap) textColored(col::Warn, "No live feed here. Prices as of %s (%s).", ago(w, snap->t).c_str(), fmtDate(snap->t).c_str());
        else textColored(col::Warn, "No intel on this market: send a ship, or buy a market report.");
        ImGui::SameLine();
        ImGui::PushID("rep");
        if (ImGui::SmallButton(("Market report for " + w.regions[reg].name + " (" + fmtMoneyShort(w.intelPrice(reg, false)) + ")").c_str())) {
            Command c; c.type = Cmd::BuyIntel; c.a = reg; c.flag = false; run(c);
        }
        ImGui::PopID();
    }
    if (const GalaxyEvent* ev = w.eventAt(mktStation, mktItem)) {
        bool ban = w.embargoed(mktStation, mktItem);
        textColored(ban ? col::Bad : col::Warn, "%s: %s.", eventKindName(ev->kind), ev->headline.c_str());
    }

    if (ImGui::BeginTabBar("mtabs")) {
        auto tabFlags = [&](int i) { return mktTabReq == i ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Order book", nullptr, tabFlags(0))) {
            float half = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
            if (!live) {
                // only what the snapshot recorded: the top of each side of the book
                bkSells.clear();
                bkBuys.clear();
                if (snap)
                    if (const SnapRow* r = snap->find(mktItem)) {
                        if (r->ask) { Order o; o.price = r->ask; o.qty = r->askQty; bkSells.push_back(o); }
                        if (r->bid) { Order o; o.price = r->bid; o.qty = r->bidQty; o.buy = true; bkBuys.push_back(o); }
                    }
            }
            ImGui::BeginGroup();
            header(live ? "Sell orders (you buy)" : "Best offer when last seen");
            ImGui::PushItemWidth(half);
            ImGui::BeginChild("asks", ImVec2(half, 240));
            bookTable(*this, "asks_t", bkSells, false);
            ImGui::EndChild();
            ImGui::PopItemWidth();
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            header(live ? "Buy orders (you sell)" : "Best bid when last seen");
            ImGui::BeginChild("bids", ImVec2(half, 240));
            bookTable(*this, "bids_t", bkBuys, true);
            ImGui::EndChild();
            ImGui::EndGroup();

            // ---- trading panel
            static int64_t buyQty = 0, sellQty = 0, ordQty = 0;
            static Money buyMax = 0, sellMin = 0, ordPrice = 0;
            static int ordSide = 1;
            static int lastItem = -1;
            static Id lastSt = NONE;
            Money ask = bkSells.empty() ? 0 : bkSells.front().price, bid = bkBuys.empty() ? 0 : bkBuys.front().price;
            if (live) ask = w.bestAsk(mktStation, mktItem, nullptr, w.player), bid = w.bestBid(mktStation, mktItem, nullptr, w.player);
            if (lastItem != mktItem || lastSt != mktStation) {
                lastItem = mktItem;
                lastSt = mktStation;
                buyQty = isShipItem(mktItem) || isBpo(mktItem) || isModule(mktItem) || isBpc(mktItem) || isStructure(mktItem) ? 1 : 1000;
                sellQty = mine;
                buyMax = ask ? ask * 110 / 100 : it.base;
                sellMin = bid ? bid * 90 / 100 : it.base / 2;
                ordPrice = ask ? ask - std::max<Money>(1, ask / 100) : it.base;
                ordQty = std::max<int64_t>(1, mine);
                ordSide = mine > 0 ? 1 : 0;
            }
            if (ImGui::BeginTable("trade", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableNextColumn();
                header("Buy now");
                inputQty("Quantity##b", buyQty);
                inputCredits("Max price##b", buyMax);
                auto [bq, bc] = walk(bkSells, buyQty, buyMax, true, w.player);
                textDim("Fills %s for %s", fmtInt(bq).c_str(), fmtMoneyShort(bc).c_str());
                if (bq > 0) textDim("avg %s each", fmtMoney(bc / bq, false).c_str());
                ImGui::BeginDisabled(!live);
                if (accentButton("Buy", ImVec2(110, 0))) {
                    Command c; c.type = Cmd::MarketBuy; c.a = mktStation; c.item = mktItem; c.qty = buyQty; c.price = buyMax;
                    run(c);
                }
                ImGui::EndDisabled();
                if (!live) textDim("Needs a live feed.");
                ImGui::TableNextColumn();
                header("Sell now");
                inputQty("Quantity##s", sellQty);
                ImGui::SameLine();
                if (ImGui::SmallButton("All")) sellQty = mine;
                inputCredits("Min price##s", sellMin);
                auto [sq, sv] = walk(bkBuys, std::min(sellQty, mine), sellMin, false, w.player);
                Money net = (Money)(sv * (1.0 - w.salesTax(w.player, mktStation)));
                textDim("Sells %s for %s after tax", fmtInt(sq).c_str(), fmtMoneyShort(net).c_str());
                ImGui::BeginDisabled(!live);
                if (accentButton("Sell", ImVec2(110, 0))) {
                    Command c; c.type = Cmd::MarketSell; c.a = mktStation; c.item = mktItem; c.qty = sellQty; c.price = sellMin;
                    run(c);
                }
                ImGui::EndDisabled();
                if (!live) textDim("Needs a live feed.");
                ImGui::TableNextColumn();
                header("Post an order");
                ImGui::RadioButton("Buy order", &ordSide, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Sell order", &ordSide, 1);
                inputCredits("Price##o", ordPrice);
                inputQty("Quantity##o", ordQty);
                Money val = ordPrice * ordQty;
                textDim("Value %s, broker fee %s", fmtMoneyShort(val).c_str(), fmtMoneyShort((Money)(val * w.brokerFee(w.player))).c_str());
                if (accentButton("Place order", ImVec2(110, 0))) {
                    Command c; c.type = Cmd::PlaceOrder; c.a = mktStation; c.item = mktItem; c.qty = ordQty; c.price = ordPrice;
                    c.flag = ordSide == 0;
                    run(c);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Price history", nullptr, tabFlags(1))) {
            std::vector<double> x, hi, lo, close, vol;
            int32_t lastDay = live ? INT32_MAX : snap ? (int32_t)(snap->t / MINUTES_PER_DAY) : -100000;
            for (auto& b : bk.hist) {
                if (b.day >= lastDay) break;  // out of sight, history stops at the last snapshot
                x.push_back(b.day);
                hi.push_back((double)b.high);
                lo.push_back((double)b.low);
                close.push_back((double)b.close);
                vol.push_back((double)b.volume);
            }
            if (bk.tradedToday && live) {
                x.push_back(bk.today.day);
                hi.push_back((double)bk.today.high);
                lo.push_back((double)bk.today.low);
                close.push_back((double)bk.today.close);
                vol.push_back((double)bk.today.volume);
            }
            if (x.size() < 2)
                textDim(live ? "No recent trading here. Away from the hubs, history is only kept where corporations trade."
                             : "Out of sight: history up to your last snapshot only, and there is not enough of it.");
            else {
                float hgt = ImGui::GetContentRegionAvail().y;
                if (ImPlot::BeginPlot("##price", ImVec2(-1, hgt * 0.68f), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
                    ImPlot::SetupAxes("day", "price (cr)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                    ImPlot::SetupLegend(ImPlotLocation_NorthWest);
                    ImPlot::PlotShaded("Daily range", x.data(), lo.data(), hi.data(), (int)x.size(),
                                       {ImPlotProp_FillColor, col::v4(col::Accent), ImPlotProp_FillAlpha, 0.18f});
                    ImPlot::PlotLine("Close", x.data(), close.data(), (int)x.size(), {ImPlotProp_LineColor, col::v4(col::Accent), ImPlotProp_LineWeight, 2.0f});
                    double ref = toCr(w.refPrice(mktStation, mktItem));
                    double xs[2] = {x.front(), x.back()}, ys[2] = {ref, ref};
                    ImPlot::PlotLine("Station reference now", xs, ys, 2, {ImPlotProp_LineColor, col::v4(col::Warn)});
                    ImPlot::EndPlot();
                }
                if (ImPlot::BeginPlot("##vol", ImVec2(-1, -1), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend | ImPlotFlags_NoBoxSelect)) {
                    ImPlot::SetupAxes("day", "volume", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                    ImPlot::PlotBars("Volume", x.data(), vol.data(), (int)x.size(), 0.7, {ImPlotProp_FillColor, col::v4(col::Neutral), ImPlotProp_FillAlpha, 0.6f});
                    ImPlot::EndPlot();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All stations", nullptr, tabFlags(2))) {
            tut.marketAllSeen = true;
            static bool onlyLive = true;
            static int radius = 12;
            ImGui::Checkbox("Only stations with orders", &onlyLive);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            ImGui::SliderInt("jumps from here", &radius, 2, 40);
            ImGui::SameLine();
            textDim("Click a row to open that book. Spread = best bid there vs best ask here.");
            struct Row { Id st; Money ask, bid; int64_t ad, bd; int j; float risk; Zone z; Tick asOf; };
            std::vector<Row> rows;
            std::vector<int> dist;
            std::vector<Id> near = w.systemsWithin(st.system, radius, &dist);
            for (size_t k = 0; k < near.size() && rows.size() < 600; k++)
                for (Id sid : w.systems[near[k]].stations) {
                    const Station& s2 = w.stations[sid];
                    if (s2.dead) continue;
                    Row r{sid, 0, 0, 0, 0, dist[k], w.systems[s2.system].risk, w.systems[s2.system].zone, w.now};
                    if (w.seen(s2.system)) {
                        r.ask = w.bestAsk(sid, mktItem, &r.ad, w.player);
                        r.bid = w.bestBid(sid, mktItem, &r.bd, w.player);
                    } else {
                        auto itS = w.intel.find(sid);
                        r.asOf = itS == w.intel.end() ? -1 : itS->second.t;
                        if (itS != w.intel.end())
                            if (const SnapRow* sr = itS->second.find(mktItem)) { r.ask = sr->ask; r.ad = sr->askQty; r.bid = sr->bid; r.bd = sr->bidQty; }
                    }
                    if (onlyLive && !r.ask && !r.bid) continue;
                    rows.push_back(r);
                }
            if (beginTable("allst", 10, ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableSetupColumn("Station", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Zone");
                ImGui::TableSetupColumn("Risk");
                ImGui::TableSetupColumn("Jumps", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("Best ask");
                ImGui::TableSetupColumn("Ask depth");
                ImGui::TableSetupColumn("Best bid", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Bid depth");
                ImGui::TableSetupColumn("Bid vs ask here");
                ImGui::TableSetupColumn("Prices as of");
                ImGui::TableHeadersRow();
                int sc = 3;
                bool asc = true;
                sortSpec(sc, asc);
                Money hereAsk = live ? w.bestAsk(mktStation, mktItem, nullptr, w.player) : 0;
                if (!live && snap)
                    if (const SnapRow* sr = snap->find(mktItem)) hereAsk = sr->ask;
                std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                    double va = 0, vb = 0;
                    switch (sc) {
                        case 0: return asc ? w.stations[a.st].name < w.stations[b.st].name : w.stations[a.st].name > w.stations[b.st].name;
                        case 1: va = (int)a.z; vb = (int)b.z; break;
                        case 2: va = a.risk; vb = b.risk; break;
                        case 3: va = a.j; vb = b.j; break;
                        case 4: va = a.ask ? (double)a.ask : 1e18; vb = b.ask ? (double)b.ask : 1e18; break;
                        case 5: va = (double)a.ad; vb = (double)b.ad; break;
                        case 6: case 8: va = (double)a.bid; vb = (double)b.bid; break;
                        case 7: va = (double)a.bd; vb = (double)b.bd; break;
                        case 9: va = (double)a.asOf; vb = (double)b.asOf; break;
                    }
                    return asc ? va < vb : va > vb;
                });
                ImGuiListClipper clip;
                clip.Begin((int)rows.size());
                while (clip.Step())
                    for (int ri = clip.DisplayStart; ri < clip.DisplayEnd; ri++) {
                        const Row& r = rows[ri];
                        ImGui::TableNextRow();
                        if (r.st == mktStation) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, withAlpha(col::Accent, 0.10f));
                        ImGui::TableNextColumn();
                        ImGui::PushID(r.st);
                        if (ImGui::Selectable(w.stations[r.st].name.c_str(), r.st == mktStation, ImGuiSelectableFlags_SpanAllColumns)) {
                            mktStation = r.st;
                            mktTabReq = 0;
                        }
                        ImGui::PopID();
                        ImGui::TableNextColumn(); zoneBadge(r.z);
                        ImGui::TableNextColumn(); riskBadge(r.risk);
                        ImGui::TableNextColumn(); cellInt(r.j);
                        ImGui::TableNextColumn(); if (r.ask) cellMoney(r.ask); else rightText("-", col::Dim);
                        ImGui::TableNextColumn(); if (r.ad) cellInt(r.ad); else rightText("-", col::Dim);
                        ImGui::TableNextColumn(); if (r.bid) rightText(fmtMoney(r.bid, false), col::Good); else rightText("-", col::Dim);
                        ImGui::TableNextColumn(); if (r.bd) cellInt(r.bd); else rightText("-", col::Dim);
                        ImGui::TableNextColumn();
                        if (r.bid && hereAsk) {
                            double pct = ((double)r.bid / (double)hereAsk - 1.0) * 100.0;
                            char b[24];
                            std::snprintf(b, sizeof b, "%+.1f%%", pct);
                            rightText(b, pct > 6 ? col::Good : pct < 0 ? col::Dim : col::Text);
                        } else rightText("-", col::Dim);
                        ImGui::TableNextColumn();
                        if (r.asOf == w.now) textColored(col::Good, "live");
                        else if (r.asOf < 0) textDim("no intel");
                        else textColored(w.now - r.asOf > 2 * MINUTES_PER_DAY ? col::Warn : col::Dim, "%s", ago(w, r.asOf).c_str());
                    }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("My orders", nullptr, tabFlags(3))) {
            struct R { Id st; int item; const Order* o; };
            std::vector<R> rows;
            for (int64_t spot : w.pc().orderSpots) {
                Id sid = (Id)(spot / HANGAR_KEY);
                int i = (int)(spot % HANGAR_KEY);
                const Book* b2 = w.stations[sid].findBook(i);
                if (!b2) continue;
                for (auto& o : b2->sells) if (o.corp == w.player) rows.push_back({sid, i, &o});
                for (auto& o : b2->buys) if (o.corp == w.player) rows.push_back({sid, i, &o});
            }
            if (rows.empty()) textDim("You have no open orders. Post one from the Order book tab.");
            int64_t cancelId = 0;
            if (!rows.empty() && beginTable("myorders", 7)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Station", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Item");
                ImGui::TableSetupColumn("Side");
                ImGui::TableSetupColumn("Price");
                ImGui::TableSetupColumn("Remaining");
                ImGui::TableSetupColumn("Expires in");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (auto& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(w.stations[r.st].name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)r.o->id);
                    if (ImGui::Selectable(item(r.item).name.c_str())) { mktStation = r.st; mktItem = r.item; mktTabReq = 0; }
                    ImGui::TableNextColumn(); textColored(r.o->buy ? col::Good : col::Accent, r.o->buy ? "Buy" : "Sell");
                    ImGui::TableNextColumn(); cellMoney(r.o->price);
                    ImGui::TableNextColumn(); rightText(fmtInt(r.o->qty) + " / " + fmtInt(r.o->qty0));
                    ImGui::TableNextColumn(); textDim("%s", fmtDuration(r.o->expires - w.now).c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Cancel")) cancelId = r.o->id;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (cancelId) { Command c; c.type = Cmd::CancelOrder; c.qty = cancelId; run(c); }
            ImGui::EndTabItem();
        }
        mktTabReq = -1;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace nb
