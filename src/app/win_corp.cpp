// NebulaOne - Corporation overview and the Wallet & Ledger.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

static const char* ratingLetter(int r) {
    static const char* L[] = {"A", "B", "C", "D", "E"};
    return L[std::clamp(r, 0, 4)];
}

void App::winCorp() {
    if (!beginWindow(*this, W_CORP, ImVec2(70, 70), ImVec2(640, 620))) { ImGui::End(); return; }
    World& w = world;
    Corp& c = w.pc();
    ImGui::PushFont(fonts.title, 24.0f);
    textColored(col::Accent, "%s", c.ticker.c_str());
    ImGui::SameLine(0, 10);
    ImGui::TextUnformatted(c.name.c_str());
    ImGui::PopFont();

    textDim("Home: %s  ·  founded %s  ·  %s", w.stations[c.home].name.c_str(), fmtDate(c.founded).substr(0, 10).c_str(), c.listed ? "listed on the Exchange" : "privately held");

    int ships = 0, inFleets = 0, staff = 0;
    Money wages = 0;
    for (Id sid : c.shipList) {
        const Ship& s = w.ships[sid];
        if (s.alive && s.corp == w.player) { ships++; inFleets += s.fleet != NONE; }
    }
    for (auto& p : w.people)
        if (p.alive && p.employer == w.player) { staff++; wages += p.wage; }
    Money wk = c.stats.size() >= 8 ? c.stats[c.stats.size() - 8].netWorth : c.stats.front().netWorth;
    ImGui::Spacing();
    if (ImGui::BeginTable("kpis", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Wallet", fmtMoneyShort(c.wallet), c.wallet < 0 ? col::Bad : col::Text);
        ImGui::TableNextColumn(); kpi("Net worth", fmtMoneyShort(c.netWorth));
        ImGui::TableNextColumn();
        kpi("7-day change", (c.netWorth >= wk ? "+" : "") + fmtMoneyShort(c.netWorth - wk), c.netWorth >= wk ? col::Good : col::Bad);
        ImGui::TableNextColumn(); kpi("Credit rating", ratingLetter(c.rating), c.rating <= 1 ? col::Good : c.rating >= 3 ? col::Warn : col::Text);
        ImGui::TableNextColumn(); kpi("Ships", std::to_string(ships) + " (" + std::to_string(inFleets) + " in fleets)");
        ImGui::TableNextColumn(); kpi("Staff", std::to_string(staff));
        ImGui::TableNextColumn(); kpi("Daily wages", fmtMoneyShort(wages));
        ImGui::TableNextColumn();
        char sb[32];
        std::snprintf(sb, sizeof sb, "%+.1f%s", c.charterStanding, w.isOutlaw(w.player) ? " OUTLAW" : "");
        kpi("Charter standing", sb, c.charterStanding < -2 ? col::Bad : col::Text);
        ImGui::EndTable();
    }
    header("Net worth and cash");
    if (c.stats.size() >= 2 && ImPlot::BeginPlot("##nw", ImVec2(-1, 170), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
        std::vector<double> x, nw, cash;
        for (auto& d : c.stats) { x.push_back(d.day); nw.push_back(toCr(d.netWorth) / 1e6); cash.push_back(toCr(d.wallet) / 1e6); }
        ImPlot::SetupAxes("day", "M cr", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        ImPlot::PlotShaded("Net worth", x.data(), nw.data(), (int)x.size(), 0.0, {ImPlotProp_FillColor, col::v4(col::Accent), ImPlotProp_FillAlpha, 0.12f});
        ImPlot::PlotLine("Net worth", x.data(), nw.data(), (int)x.size(), {ImPlotProp_LineColor, col::v4(col::Accent), ImPlotProp_LineWeight, 2.0f});
        ImPlot::PlotLine("Wallet", x.data(), cash.data(), (int)x.size(), {ImPlotProp_LineColor, col::v4(col::Neutral)});
        ImPlot::EndPlot();
    } else if (c.stats.size() < 2) {
        textDim("History appears after the first game day.");
    }
    header("Executive board");
    for (int seat = 0; seat < SEAT_COUNT; seat++) {
        ImGui::PushID(seat);
        static const char* effect[SEAT_COUNT] = {"Broker fee and sales tax up to -30%", "Refining yield up to +10 pts, jobs up to 20% faster",
                                                 "Encounters up to -30%, combat +10%", "Travel time up to -10%", "Mining yield up to +10%",
                                                 "Broker fee and sales tax up to -15% more", "Espionage odds and counter-intelligence"};
        ImGui::TextUnformatted(seatName(seat));
        ImGui::SameLine(190);
        Id p = c.exec[seat];
        std::string cur = p == NONE ? "Vacant" : w.people[p].name + "  (mgmt " + std::to_string(w.people[p].mgmt) + ")";
        ImGui::SetNextItemWidth(230);
        if (ImGui::BeginCombo("##seat", cur.c_str())) {
            if (ImGui::Selectable("Vacant", p == NONE)) { Command cm; cm.type = Cmd::AssignExec; cm.a = NONE; cm.b = seat; run(cm); }
            for (auto& pp : w.people) {
                if (!pp.alive || pp.employer != w.player || pp.role != Role::Executive) continue;
                ImGui::PushID(pp.id);
                std::string lab = pp.name + "  mgmt " + std::to_string(pp.mgmt) + (pp.seat == seat ? "  (specialist)" : "");
                if (ImGui::Selectable(lab.c_str(), p == pp.id)) { Command cm; cm.type = Cmd::AssignExec; cm.a = pp.id; cm.b = seat; run(cm); }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        textDim("%s (now %.0f%%)", effect[seat], w.execBonus(w.player, seat) * 100);
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Recruit executives...")) openWin(W_PEOPLE);
    ImGui::SameLine();
    if (ImGui::SmallButton("Divisions...")) openWin(W_DIVS);
    ImGui::SameLine();
    textDim("Directors in the division seats can run their divisions for you.");

    header("Goals");
    auto goal = [&](const char* label, bool done, float progress) {
        textColored(done ? col::Good : col::Dim, done ? "[x]" : "[ ]");
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        if (!done) {
            ImGui::SameLine(330);
            progressBar(progress, ImVec2(160, 0));
        }
    };
    goal("Reach 10M cr net worth", w.pstats.bestNetWorth >= 10000000 * CR, (float)(toCr(c.netWorth) / 1e7));
    goal("Build your first ship from a blueprint", w.pstats.manufactured > 0, 0);
    goal("Mine 50k units outside the Charter", w.pstats.riskyMinedUnits >= 50000, w.pstats.riskyMinedUnits / 50000.0f);
    goal("Complete 5 contracts from the board", w.pstats.contractsDone >= 5, w.pstats.contractsDone / 5.0f);
    goal("Reach 100M cr net worth", w.pstats.bestNetWorth >= 100000000 * CR, (float)(toCr(c.netWorth) / 1e8));
    goal("Anchor an outpost in the Frontier", w.pstats.outposts > 0, 0);
    goal("List your corporation on the Exchange", c.listed, (float)(toCr(c.netWorth) / 5e7));
    goal("Find three hidden sites with a survey", w.pstats.sitesFound >= 3, w.pstats.sitesFound / 3.0f);
    goal("Salvage a derelict or relic cache", w.pstats.salvaged > 0, 0);
    goal("Win reparations in a war", w.pstats.warsWon > 0, 0);
    header("Livery");
    int liv = c.livery;
    if (liveryPicker("##liv", liv)) {
        Command cm; cm.type = Cmd::SetLivery; cm.qty = liv; run(cm, false);
        settings.livery = liv;
        saveSettings();
    }
    ImGui::SameLine();
    textDim("%s: your fleets, outposts and the whole interface wear it.", liveryName(c.livery));
    ImGui::End();
}

// ------------------------------------------------------------------ wallet --
void App::winWallet() {
    if (!beginWindow(*this, W_WALLET, ImVec2(90, 90), ImVec2(760, 560))) { ImGui::End(); return; }
    World& w = world;
    Corp& c = w.pc();
    if (ImGui::BeginTabBar("wallettabs")) {
        if (ImGui::BeginTabItem("Journal")) {
            static int catFilter = -1;
            static char text[64] = "";
            ImGui::SetNextItemWidth(200);
            if (ImGui::BeginCombo("##cat", catFilter < 0 ? "All categories" : catLabel((Cat)catFilter))) {
                if (ImGui::Selectable("All categories", catFilter < 0)) catFilter = -1;
                for (int i = 0; i < CAT_COUNT; i++)
                    if (ImGui::Selectable(catLabel((Cat)i), catFilter == i)) catFilter = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            ImGui::InputTextWithHint("##text", "search memo", text, sizeof text);
            ImGui::SameLine();
            textDim("%d entries", (int)c.ledger.size());
            std::vector<int> rows;
            std::string f = upper(text);
            for (int i = (int)c.ledger.size() - 1; i >= 0; i--) {
                const LedgerLine& l = c.ledger[i];
                if (catFilter >= 0 && (int)l.cat != catFilter) continue;
                if (!f.empty() && upper(l.memo).find(f) == std::string::npos) continue;
                rows.push_back(i);
            }
            if (beginTable("journal", 4)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 130);
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 140);
                ImGui::TableSetupColumn("Memo", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                ImGuiListClipper clip;
                clip.Begin((int)rows.size());
                while (clip.Step())
                    for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                        const LedgerLine& l = c.ledger[rows[r]];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); monoText(fmtDate(l.t), col::Dim);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(catLabel(l.cat));
                        ImGui::TableNextColumn(); cellMoney(l.amount, true);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(l.memo.c_str());
                    }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cash flow")) {
            auto sumDays = [&](int cat, int days) {
                Money v = days == 0 ? c.catToday[cat] : 0;
                int k = 0;
                for (auto it = c.catDays.rbegin(); it != c.catDays.rend() && k < days; ++it, ++k) v += (*it)[cat];
                if (days > 0) v += c.catToday[cat];
                return v;
            };
            const Cat operating[] = {Cat::MarketSale, Cat::MarketPurchase, Cat::BrokerFee, Cat::SalesTax, Cat::RefineFee,
                                     Cat::ManufactureFee, Cat::Wages, Cat::Hiring, Cat::Repairs, Cat::Insurance,
                                     Cat::InsurancePayout, Cat::Bounty, Cat::Dividends, Cat::Other};
            const Cat financing[] = {Cat::LoanIn, Cat::LoanRepay, Cat::LoanInterest, Cat::LendOut, Cat::LendRepaid, Cat::ShareTrade};
            if (ImGui::BeginTable("cf", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Today", ImGuiTableColumnFlags_WidthFixed, 140);
                ImGui::TableSetupColumn("Last 7 days", ImGuiTableColumnFlags_WidthFixed, 140);
                ImGui::TableSetupColumn("Last 30 days", ImGuiTableColumnFlags_WidthFixed, 140);
                ImGui::TableHeadersRow();
                auto block = [&](const char* title, const Cat* cats, int n) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    textColored(col::Accent, "%s", upper(title).c_str());
                    Money tot[3] = {};
                    for (int i = 0; i < n; i++) {
                        int ci = (int)cats[i];
                        Money v[3] = {sumDays(ci, 0), sumDays(ci, 6), sumDays(ci, 29)};
                        if (!v[0] && !v[1] && !v[2]) continue;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("   %s", catLabel(cats[i]));
                        for (int k = 0; k < 3; k++) { ImGui::TableNextColumn(); cellMoney(v[k], true); tot[k] += v[k]; }
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::PushFont(fonts.bold, 0); ImGui::Text("   Total %s", title); ImGui::PopFont();
                    for (int k = 0; k < 3; k++) { ImGui::TableNextColumn(); cellMoney(tot[k], true); }
                    return std::array<Money, 3>{tot[0], tot[1], tot[2]};
                };
                auto a = block("operating", operating, (int)(sizeof operating / sizeof *operating));
                auto b = block("financing", financing, (int)(sizeof financing / sizeof *financing));
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::PushFont(fonts.bold, 0); ImGui::TextUnformatted("NET CHANGE IN CASH"); ImGui::PopFont();
                for (int k = 0; k < 3; k++) { ImGui::TableNextColumn(); cellMoney(a[k] + b[k], true); }
                ImGui::EndTable();
            }
            textDim("Buying stock to sell later shows as a cash outflow now; check the balance sheet for what it is worth.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Balance sheet")) {
            Money inv = 0, shipsV = 0, cargo = 0, escrow = 0, shares = 0, recv = 0, owe = 0;
            Money outposts = 0, collateral = 0;
            for (auto& [k, q] : c.hangar) inv += w.valueStock((Id)(k / HANGAR_KEY), (int)(k % HANGAR_KEY), q);
            for (Id sid : c.shipList) {
                const Ship& s = w.ships[sid];
                if (s.alive && s.corp == w.player) shipsV += (Money)(w.shipValue(s) * (0.6 + 0.4 * s.hull / std::max(1.0, w.stats(s).hull)));
            }
            for (Id fid : c.fleetList)
                if (w.fleets[fid].alive && w.fleets[fid].corp == w.player) cargo += w.fleetCargoValue(w.fleets[fid]);
            for (Id o : w.outpostIds)
                if (!w.stations[o].dead && w.stations[o].owner == w.player) outposts += (Money)(item(OUTPOST_KIT).base * (0.5 + 0.5 * w.stations[o].hull));
            for (auto& ct : w.contracts)
                if (ct.state == ContractState::Accepted && ct.contractor == w.player) collateral += ct.collateral;
            escrow = w.computeNetWorth(w.player);  // computed below as remainder
            for (auto& [cid, n] : c.shares) shares += w.corps[cid].sharePrice * n;
            for (auto& l : w.loans) {
                if (l.status != 0) continue;
                if (l.lender == w.player) recv += l.remaining * 9 / 10;
                if (l.borrower == w.player) owe += l.remaining;
            }
            Money nw = escrow;
            escrow = nw - (c.wallet + inv + shipsV + cargo + shares + recv - owe + outposts + collateral);
            struct Row { const char* label; Money v; };
            Row rows[] = {{"Cash", c.wallet}, {"Hangar stock (liquidation value)", inv}, {"Ships (hull-adjusted)", shipsV},
                          {"Cargo aboard fleets", cargo}, {"Market orders (escrow)", escrow}, {"Shares at market", shares},
                          {"Outposts", outposts}, {"Courier collateral held", collateral},
                          {"Loans receivable (90%)", recv}, {"Loans payable", -owe}};
            if (ImGui::BeginTable("bs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(480, 0))) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 170);
                for (auto& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.label);
                    ImGui::TableNextColumn(); cellMoney(r.v, r.v < 0);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::PushFont(fonts.bold, 0); ImGui::TextUnformatted("NET WORTH"); ImGui::PopFont();
                ImGui::TableNextColumn(); cellMoney(nw);
                ImGui::EndTable();
            }
            textDim("Stock beyond what a station can absorb is valued at 30%%: a mountain of ore is not worth its sticker price.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ------------------------------------------------------------ corporations --
void App::winCorps() {
    if (!beginWindow(*this, W_CORPS, ImVec2(120, 110), ImVec2(900, 520))) { ImGui::End(); return; }
    World& w = world;
    if (beginTable("corps", 10, ImGuiTableFlags_Sortable, ImVec2(0, selCorp != NONE ? -230 : 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Corporation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ticker");
        ImGui::TableSetupColumn("Profile");
        ImGui::TableSetupColumn("Net worth", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Ships");
        ImGui::TableSetupColumn("Lost / kills");
        ImGui::TableSetupColumn("Their view of you");
        ImGui::TableSetupColumn("Charter");
        ImGui::TableSetupColumn("Share price");
        ImGui::TableSetupColumn("Home");
        ImGui::TableHeadersRow();
        std::vector<Id> ids;
        std::vector<int> hulls(w.corps.size(), 0);
        for (auto& c : w.corps)
            for (Id sid : c.shipList) hulls[c.id] += w.ships[sid].alive && w.ships[sid].corp == c.id;
        for (auto& c : w.corps)
            if (c.alive) ids.push_back(c.id);
        int sc = 3;
        bool asc = false;
        sortSpec(sc, asc);
        std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
            const Corp &A = w.corps[a], &B = w.corps[b];
            double va = 0, vb = 0;
            switch (sc) {
                case 0: return asc ? A.name < B.name : A.name > B.name;
                case 1: return asc ? A.ticker < B.ticker : A.ticker > B.ticker;
                case 2: va = (int)A.arch; vb = (int)B.arch; break;
                case 3: va = (double)A.netWorth; vb = (double)B.netWorth; break;
                case 4: va = hulls[a]; vb = hulls[b]; break;
                case 5: va = A.shipsLost; vb = B.shipsLost; break;
                case 6: va = w.standing(a, w.player); vb = w.standing(b, w.player); break;
                case 7: va = A.charterStanding; vb = B.charterStanding; break;
                case 8: va = (double)A.sharePrice; vb = (double)B.sharePrice; break;
                default: break;
            }
            return asc ? va < vb : va > vb;
        });
        for (Id id : ids) {
            const Corp& c = w.corps[id];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(id);
            if (ImGui::Selectable(c.name.c_str(), selCorp == id, ImGuiSelectableFlags_SpanAllColumns)) selCorp = id;
            ImGui::PopID();
            ImGui::TableNextColumn(); monoText(c.ticker, c.player ? col::Accent : col::Text);
            ImGui::TableNextColumn();
            ImU32 pc = c.arch == Arch::Pirate || c.arch == Arch::Ganker ? col::Bad : c.arch == Arch::Player ? col::Accent : col::Text;
            textColored(pc, "%s", c.player ? "You" : archName(c.arch));
            ImGui::TableNextColumn(); rightText(fmtMoneyShort(c.netWorth, false));
            ImGui::TableNextColumn(); cellInt(hulls[id]);
            ImGui::TableNextColumn(); rightText(std::to_string(c.shipsLost) + " / " + std::to_string(c.shipsKilled));
            ImGui::TableNextColumn();
            if (!c.player) {
                char b[32];
                float sv = w.standing(id, w.player);
                const char* rel = w.atWar(id, w.player) ? " WAR" : w.allied(id, w.player) ? " ALLY" : w.nonAggression(id, w.player) ? " PACT" : "";
                std::snprintf(b, sizeof b, "%+.1f%s", sv, rel);
                rightText(b, w.atWar(id, w.player) || sv < -2 ? col::Bad : w.allied(id, w.player) || sv > 2 ? col::Good : col::Text);
            }
            ImGui::TableNextColumn();
            {
                char b[24];
                std::snprintf(b, sizeof b, "%+.1f%s", c.charterStanding, w.isOutlaw(id) ? " OUTLAW" : "");
                rightText(b, w.isOutlaw(id) ? col::Bad : col::Text);
            }
            ImGui::TableNextColumn();
            if (!c.player || c.listed) rightText(fmtMoney(c.sharePrice, false)); else textDim("private");
            ImGui::TableNextColumn(); textDim("%s", w.stations[c.home].name.c_str());
        }
        ImGui::EndTable();
    }
    if (selCorp != NONE && selCorp < (Id)w.corps.size()) {
        const Corp& c = w.corps[selCorp];
        ImGui::Separator();
        ImGui::PushFont(fonts.bold, kFontHead);
        ImGui::Text("%s  [%s]", c.name.c_str(), c.ticker.c_str());
        ImGui::PopFont();
        static const char* blurb[] = {"Your corporation.",
                                      "Runs trade routes between stations. Closes the price gaps you are hunting.",
                                      "Mines, refines and sells minerals. Sets the price of what you mine.",
                                      "Buys minerals and builds ships to sell. Your best customer for minerals.",
                                      "Raids haulers and miners in Contract space, beyond the Wardens' reach.",
                                      "Suicide-attacks rich, unescorted haulers in Charter space before the Wardens arrive.",
                                      "Hunts outlaws for bounties. Their patrols pull the risk ratings of Contract space down."};
        textDim("%s", blurb[(int)c.arch]);
        if (!c.player) {
            int64_t held = w.pc().shares.count(c.id) ? w.pc().shares.at(c.id) : 0;
            textDim("You hold %s of %s shares (%.1f%%). Over half lets you take the company over from the Boardroom.",
                    fmtInt(held).c_str(), fmtInt(c.sharesOut).c_str(), 100.0 * held / std::max<int64_t>(1, c.sharesOut));
            ImGui::SameLine();
            if (ImGui::SmallButton("Trade shares")) { openWin(W_FINANCE); finTabReq = 3; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Diplomacy")) openWin(W_DIPLO);
        }
        if (c.shareHist.size() > 2 && ImPlot::BeginPlot("##sp", ImVec2(-1, 150), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
            std::vector<double> y;
            for (Money m : c.shareHist) y.push_back(toCr(m));
            ImPlot::SetupAxes(nullptr, "share price (cr)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotLine("price", y.data(), (int)y.size(), 1.0, 0.0, {ImPlotProp_LineColor, col::v4(col::Accent)});
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

// ----------------------------------------------------------------- log -----
void App::winLog() {
    if (!beginWindow(*this, W_LOG, ImVec2(140, 400), ImVec2(760, 360))) { ImGui::End(); return; }
    static int filter = 1;
    const char* names[] = {"All", "Mine", "Market", "Combat", "News"};
    for (int i = 0; i < 5; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(names[i], filter == i)) filter = i;
    }
    if (beginTable("log", 3)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        std::vector<int> rows;
        for (int i = (int)world.log.size() - 1; i >= 0; i--) {
            const LogEntry& e = world.log[i];
            bool ok = filter == 0 || (filter == 1 && e.mine) || (filter == 2 && e.chan == Chan::Market) ||
                      (filter == 3 && e.chan == Chan::Combat) || (filter == 4 && e.chan == Chan::News);
            if (ok) rows.push_back(i);
        }
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const LogEntry& e = world.log[rows[r]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); monoText(fmtDate(e.t), col::Dim);
                ImGui::TableNextColumn();
                if (e.system != NONE) {
                    ImGui::PushID(rows[r]);
                    ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(world.systems[e.system].zone));
                    if (ImGui::Selectable(world.systems[e.system].name.c_str())) { selectSystem(e.system); centerMap(e.system); }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::TableNextColumn();
                ImU32 c = e.sev == Sev::Bad ? col::Bad : e.sev == Sev::Warn ? col::Warn : e.sev == Sev::Good ? col::Good : col::Text;
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextWrapped("%s", e.text.c_str());
                ImGui::PopStyleColor();
            }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace nb
