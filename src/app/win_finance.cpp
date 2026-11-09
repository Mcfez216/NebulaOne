// NebulaOne - Finance: the bank, loans both ways, the Exchange and insurance.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

void finFutures(App& app);  // win_capital.cpp
void finBonds(App& app);
void finShorts(App& app);
void finUnderwriting(App& app);

static const char* rating(int r) {
    static const char* L[] = {"A", "B", "C", "D", "E"};
    return L[std::clamp(r, 0, 4)];
}

void App::winFinance() {
    if (!beginWindow(*this, W_FINANCE, ImVec2(150, 100), ImVec2(900, 580))) { ImGui::End(); return; }
    World& w = world;
    Corp& me = w.pc();
    if (ImGui::BeginTabBar("fintabs")) {
        auto tf = [&](int i) { return finTabReq == i ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Bank", nullptr, tf(0))) {
            Money lim = w.bankCreditLimit(w.player);
            double rate = w.bankRate(w.player);
            if (ImGui::BeginTable("bk", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn(); kpi("Credit rating", rating(me.rating), me.rating <= 1 ? col::Good : me.rating >= 3 ? col::Warn : col::Text);
                ImGui::TableNextColumn(); kpi("Credit available", fmtMoneyShort(lim));
                char rb[32];
                std::snprintf(rb, sizeof rb, "%.1f%% / 30 days", rate * 100);
                ImGui::TableNextColumn(); kpi("Interest", rb);
                ImGui::EndTable();
            }
            header("Borrow from the Consolidated Bank");
            static Money amount = 1000000 * CR;
            static int term = 30;
            inputCredits("Amount", amount, 180);
            ImGui::SameLine();
            if (ImGui::SmallButton("Max")) amount = lim / CR * CR;
            ImGui::RadioButton("30 days", &term, 30); ImGui::SameLine();
            ImGui::RadioButton("60 days", &term, 60); ImGui::SameLine();
            ImGui::RadioButton("90 days", &term, 90);
            Money total = (Money)(amount * (1.0 + rate * term / 30.0));
            textDim("You repay %s in total: %s every day for %d days, taken automatically.", fmtMoney(total).c_str(),
                    fmtMoney(total / term + 1).c_str(), term);
            textDim("Missed installments add 1%% penalty and hurt your rating. Repaying early waives a third of the unearned interest.");
            if (accentButton("Take the loan", ImVec2(150, 0))) {
                Command c; c.type = Cmd::BankLoan; c.price = amount; c.qty = term;
                run(c);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Loans", nullptr, tf(1))) {
            bool any = false;
            if (beginTable("loans", 9)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Direction");
                ImGui::TableSetupColumn("Counterparty", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Principal");
                ImGui::TableSetupColumn("Still owed");
                ImGui::TableSetupColumn("Rate / 30d");
                ImGui::TableSetupColumn("Daily");
                ImGui::TableSetupColumn("Days left");
                ImGui::TableSetupColumn("Status");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (int i = (int)w.loans.size() - 1; i >= 0; i--) {
                    const Loan& l = w.loans[i];
                    bool borrow = l.borrower == w.player, lend = l.lender == w.player;
                    if (!borrow && !lend) continue;
                    any = true;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); textColored(borrow ? col::Warn : col::Good, borrow ? "Borrowed" : "Lent");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(borrow ? (l.lender == NONE ? "Consolidated Bank" : w.corps[l.lender].name.c_str()) : w.corps[l.borrower].name.c_str());
                    ImGui::TableNextColumn(); cellMoney(l.principal);
                    ImGui::TableNextColumn(); cellMoney(l.status == 0 ? l.remaining : 0);
                    char rb[16];
                    std::snprintf(rb, sizeof rb, "%.1f%%", l.rate30 * 100);
                    ImGui::TableNextColumn(); rightText(rb);
                    ImGui::TableNextColumn(); cellMoney(l.daily);
                    ImGui::TableNextColumn();
                    int left = l.status == 0 ? (int)std::ceil((double)l.remaining / std::max<Money>(1, l.daily)) : 0;
                    cellInt(left);
                    ImGui::TableNextColumn();
                    if (l.status == 1) textColored(col::Good, "Repaid");
                    else if (l.status == 2) textColored(col::Bad, "Defaulted");
                    else if (l.missed) textColored(col::Bad, "%d missed", l.missed);
                    else textDim("Current");
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (borrow && l.status == 0 && ImGui::SmallButton("Repay now")) { Command c; c.type = Cmd::RepayLoan; c.a = l.id; run(c); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (!any) textDim("No loans yet.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Loan board", nullptr, tf(2))) {
            textDim("Corporations asking to borrow. Fund a request and they pay you back daily, or default and you recover 30%%.");
            if (beginTable("board", 9)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Borrower", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Profile");
                ImGui::TableSetupColumn("Rating");
                ImGui::TableSetupColumn("Amount");
                ImGui::TableSetupColumn("Rate / 30d");
                ImGui::TableSetupColumn("Term");
                ImGui::TableSetupColumn("You earn");
                ImGui::TableSetupColumn("Closes in");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                int n = 0;
                for (auto& r : w.loanReqs) {
                    if (!r.open || r.expires <= w.now) continue;
                    n++;
                    const Corp& c = w.corps[r.corp];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name.c_str());
                    ImGui::TableNextColumn(); textColored(c.arch == Arch::Pirate || c.arch == Arch::Ganker ? col::Bad : col::Text, "%s", archName(c.arch));
                    ImGui::TableNextColumn(); textColored(r.rating <= 1 ? col::Good : r.rating >= 3 ? col::Bad : col::Text, "%s", rating(r.rating));
                    ImGui::TableNextColumn(); cellMoney(r.amount);
                    char rb[16];
                    std::snprintf(rb, sizeof rb, "%.1f%%", r.rate30 * 100);
                    ImGui::TableNextColumn(); rightText(rb);
                    ImGui::TableNextColumn(); rightText(std::to_string(r.days) + " d");
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort((Money)(r.amount * r.rate30 * r.days / 30.0), false), col::Good);
                    ImGui::TableNextColumn(); textDim("%s", fmtDuration(r.expires - w.now).c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID(r.id);
                    if (ImGui::SmallButton("Fund")) { Command cm; cm.type = Cmd::FundRequest; cm.a = r.id; run(cm); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (!n) textDim("No open requests right now. New ones appear most days.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Exchange", nullptr, tf(3))) {
            static Id pick = NONE;
            if (beginTable("exch", 8, ImGuiTableFlags_Sortable, ImVec2(0, pick != NONE ? -250 : 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Corporation", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Ticker");
                ImGui::TableSetupColumn("Price");
                ImGui::TableSetupColumn("7 days");
                ImGui::TableSetupColumn("30 days");
                ImGui::TableSetupColumn("Market cap", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("You hold");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();
                std::vector<Id> ids;
                for (auto& c : w.corps)
                    if (!c.player && c.alive) ids.push_back(c.id);
                auto chg = [&](const Corp& c, int d) {
                    if ((int)c.shareHist.size() <= d) return 0.0;
                    double a = (double)c.shareHist[c.shareHist.size() - 1 - d], b = (double)c.sharePrice;
                    return a > 0 ? (b / a - 1.0) * 100.0 : 0.0;
                };
                int sc = 5;
                bool asc = false;
                sortSpec(sc, asc);
                std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
                    const Corp &A = w.corps[a], &B = w.corps[b];
                    double va = 0, vb = 0;
                    switch (sc) {
                        case 0: case 1: return asc ? A.ticker < B.ticker : A.ticker > B.ticker;
                        case 2: va = (double)A.sharePrice; vb = (double)B.sharePrice; break;
                        case 3: va = chg(A, 7); vb = chg(B, 7); break;
                        case 4: va = chg(A, 30); vb = chg(B, 30); break;
                        case 5: va = (double)A.sharePrice * A.sharesOut; vb = (double)B.sharePrice * B.sharesOut; break;
                        default: {
                            auto ia = me.shares.find(a), ib = me.shares.find(b);
                            va = ia == me.shares.end() ? 0 : (double)ia->second;
                            vb = ib == me.shares.end() ? 0 : (double)ib->second;
                        }
                    }
                    return asc ? va < vb : va > vb;
                });
                for (Id id : ids) {
                    const Corp& c = w.corps[id];
                    auto it = me.shares.find(id);
                    int64_t held = it == me.shares.end() ? 0 : it->second;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(id);
                    if (ImGui::Selectable(c.name.c_str(), pick == id, ImGuiSelectableFlags_SpanAllColumns)) pick = id;
                    ImGui::PopID();
                    ImGui::TableNextColumn(); monoText(c.ticker);
                    ImGui::TableNextColumn(); cellMoney(c.sharePrice);
                    for (int d : {7, 30}) {
                        ImGui::TableNextColumn();
                        double v = chg(c, d);
                        char b[16];
                        std::snprintf(b, sizeof b, "%+.1f%%", v);
                        rightText(b, v >= 0 ? col::Good : col::Bad);
                    }
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(c.sharePrice * c.sharesOut, false));
                    ImGui::TableNextColumn(); if (held) cellInt(held); else rightText("-", col::Dim);
                    ImGui::TableNextColumn(); if (held) cellMoney(c.sharePrice * held); else rightText("-", col::Dim);
                }
                ImGui::EndTable();
            }
            if (pick != NONE) {
                const Corp& c = w.corps[pick];
                ImGui::Separator();
                ImGui::BeginGroup();
                ImGui::PushFont(fonts.bold, kFontHead);
                ImGui::Text("%s  [%s]", c.name.c_str(), c.ticker.c_str());
                ImGui::PopFont();
                textDim("%s  ·  %s shares  ·  pays 20%% of net-worth growth as a dividend every 30 days%s", archName(c.arch),
                        fmtInt(c.sharesOut).c_str(), c.poisonPill ? "  ·  poison pill armed" : "");
                static int64_t q = 1000;
                inputQty("Shares", q, 120);
                textDim("About %s. Trading more than 1%% of shares in one go moves the price against you; 0.5%% fee.",
                        fmtMoneyShort(c.sharePrice * q).c_str());
                if (accentButton("Buy", ImVec2(90, 0))) { Command cm; cm.type = Cmd::BuyShares; cm.a = pick; cm.qty = q; run(cm); }
                ImGui::SameLine();
                if (ImGui::Button("Sell", ImVec2(90, 0))) { Command cm; cm.type = Cmd::SellShares; cm.a = pick; cm.qty = q; run(cm); }
                ImGui::SameLine();
                if (ImGui::Button("Sell short", ImVec2(90, 0))) { Command cm; cm.type = Cmd::OpenShort; cm.a = pick; cm.qty = q; run(cm); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Borrow the shares and sell them: you profit if the price falls (Short selling tab).");
                ImGui::EndGroup();
                ImGui::SameLine();
                if (c.shareHist.size() > 2 && ImPlot::BeginPlot("##sh", ImVec2(-1, 200), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
                    std::vector<double> y;
                    for (Money m : c.shareHist) y.push_back(toCr(m));
                    ImPlot::SetupAxes("days", "cr", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                    ImPlot::PlotLine("px", y.data(), (int)y.size(), 1.0, 0.0, {ImPlotProp_LineColor, col::v4(col::Accent)});
                    ImPlot::EndPlot();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Insurance", nullptr, tf(4))) {
            textDim("Cover from the Charter underwriters lasts 30 days and pays 70%% of the hull if the ship is destroyed. The premium "
                    "is 3%% of hull value plus 0.2%% for every point of risk where the ship works.");
            if (beginTable("ins", 5)) {
                ImGui::TableSetupColumn("Ship", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Hull value");
                ImGui::TableSetupColumn("Premium");
                ImGui::TableSetupColumn("Cover");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (auto& s : w.ships) {
                    if (!s.alive || s.corp != w.player) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%s #%d", shipDef(s.def).name.c_str(), s.id);
                    ImGui::TableNextColumn(); cellMoney(w.shipValue(s));
                    ImGui::TableNextColumn(); cellMoney(w.insurancePremium(s));
                    ImGui::TableNextColumn();
                    bool on = s.insured && s.insuredUntil > w.now;
                    if (on) textColored(col::Good, "%s left", fmtDuration(s.insuredUntil - w.now).c_str()); else textDim("none");
                    ImGui::TableNextColumn();
                    ImGui::PushID(s.id);
                    if (!on && ImGui::SmallButton("Insure")) { Command c; c.type = Cmd::SetInsurance; c.a = s.id; c.flag = true; run(c); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Futures", nullptr, tf(5))) { finFutures(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Bonds", nullptr, tf(6))) { finBonds(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Short selling", nullptr, tf(7))) { finShorts(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Underwriting", nullptr, tf(8))) { finUnderwriting(*this); ImGui::EndTabItem(); }
        finTabReq = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace nb
