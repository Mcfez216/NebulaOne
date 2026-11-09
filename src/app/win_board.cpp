// NebulaOne - the Boardroom (IPO, the board, dividends, buybacks, takeovers)
// and the News Wire.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

void boardDefences(App& app);  // win_capital.cpp

void App::winBoard() {
    if (!beginWindow(*this, W_BOARD, ImVec2(200, 120), ImVec2(760, 600))) { ImGui::End(); return; }
    World& w = world;
    Corp& c = w.pc();
    if (!c.listed) {
        header("Go public");
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(
            "Floating part of the company on the Exchange raises cash without debt. In return you answer to a board: every "
            "30 days it compares the growth in net asset value per share with its target. Keep it happy with growth, "
            "dividends and buybacks. If confidence collapses, it forces a payout and dismisses your CFO.");
        ImGui::PopTextWrapPos();
        bool worth = c.netWorth >= 50000000 * CR, age = w.now - c.founded >= 30 * MINUTES_PER_DAY;
        textColored(worth ? col::Good : col::Bad, "%s Net worth of 50M cr (you: %s)", worth ? "[x]" : "[ ]", fmtMoneyShort(c.netWorth).c_str());
        textColored(age ? col::Good : col::Bad, "%s 30 days of accounts (you: %s)", age ? "[x]" : "[ ]", fmtDuration(w.now - c.founded).c_str());
        static float pct = 25;
        ImGui::SetNextItemWidth(240);
        ImGui::SliderFloat("Share of the company to float", &pct, 10, 49, "%.0f%%");
        Money val = w.ipoValuation();
        Money proceeds = (Money)((double)val * pct / 100.0 * 0.95);
        textDim("The underwriters value you at %s (1.2x net worth): you would raise about %s after their 5%% fee.",
                fmtMoneyShort(val).c_str(), fmtMoneyShort(proceeds).c_str());
        ImGui::BeginDisabled(!(worth && age));
        if (accentButton("List on the Exchange", ImVec2(200, 0))) { Command cm; cm.type = Cmd::GoPublic; cm.f = pct / 100.0f; run(cm); }
        ImGui::EndDisabled();
    } else {
        if (ImGui::BeginTable("kp", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn(); kpi("Share price", fmtMoney(c.sharePrice));
            ImGui::TableNextColumn(); kpi("Market cap", fmtMoneyShort(c.sharePrice * c.sharesOut));
            ImGui::TableNextColumn(); kpi("Public float", fmtInt(c.publicFloat) + " (" + std::to_string((int)(100.0 * c.publicFloat / c.sharesOut)) + "%)");
            ImGui::TableNextColumn(); kpi("Next review", fmtDuration(std::max<Tick>(0, c.nextReview - w.now)));
            ImGui::EndTable();
        }
        header("Board confidence");
        progressBar(c.board / 100.0f, ImVec2(260, 0), c.board < 20 ? col::Bad : c.board < 45 ? col::Warn : col::Good);
        ImGui::SameLine();
        ImGui::Text("%.0f / 100", c.board);
        Money nav = c.netWorth / std::max<int64_t>(1, c.sharesOut);
        double growth = c.reviewBase > 0 ? (double)nav / (double)c.reviewBase - 1.0 : 0;
        textDim("Target: +%.0f%% net asset value per share each 30 days. So far this period: %+.1f%%.", c.boardTarget * 100, growth * 100);
        if (c.shareHist.size() > 2 && ImPlot::BeginPlot("##px", ImVec2(-1, 160), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
            std::vector<double> y;
            for (Money m : c.shareHist) y.push_back(toCr(m));
            ImPlot::SetupAxes(nullptr, "cr per share", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotLine("price", y.data(), (int)y.size(), 1.0, 0.0, {ImPlotProp_LineColor, col::v4(col::Accent)});
            ImPlot::EndPlot();
        }
        header("Return capital");
        static Money div = 0;
        static int64_t back = 0;
        inputCredits("Dividend, total", div, 160);
        ImGui::SameLine();
        textDim("%s per share", fmtMoney(div / std::max<int64_t>(1, c.sharesOut), false).c_str());
        ImGui::SameLine();
        if (ImGui::Button("Declare")) { Command cm; cm.type = Cmd::DeclareDividend; cm.price = div; run(cm); }
        inputQty("Shares to buy back", back, 160);
        ImGui::SameLine();
        textDim("about %s", fmtMoneyShort(c.sharePrice * back * 105 / 100).c_str());
        ImGui::SameLine();
        if (ImGui::Button("Buy back")) { Command cm; cm.type = Cmd::Buyback; cm.qty = back; run(cm); }
        textDim("Only the float is paid: your own holding is the company's. Buying back the whole float takes you private again.");
        header("Who holds you");
        int n = 0;
        for (auto& h : w.corps) {
            auto it = h.shares.find(c.id);
            if (it == h.shares.end() || it->second <= 0) continue;
            n++;
            ImGui::Text("%s", h.name.c_str());
            ImGui::SameLine(260);
            textDim("%s shares (%.2f%%)", fmtInt(it->second).c_str(), 100.0 * it->second / c.sharesOut);
        }
        if (!n) textDim("Only the public.");
        boardDefences(*this);
    }
    header("Takeovers");
    ImGui::PushTextWrapPos(0);
    textDim("Buy more than half of a rival's shares on the Exchange (Finance window), then buy out the rest here. Everything "
            "it owns becomes yours: cash, hangars, ships, outposts and loans.");
    ImGui::PopTextWrapPos();
    std::vector<Id> stakes;
    for (auto& [cid, q] : c.shares)
        if (q > 0 && w.corps[cid].alive && !w.corps[cid].player) stakes.push_back(cid);
    if (stakes.empty()) textDim("You hold no stakes yet.");
    else if (beginTable("stakes", 6, 0, ImVec2(0, std::min(200.0f, 30.0f + 26.0f * stakes.size())))) {
        ImGui::TableSetupColumn("Corporation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Held");
        ImGui::TableSetupColumn("Stake");
        ImGui::TableSetupColumn("Their net worth");
        ImGui::TableSetupColumn("Buyout cost");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (Id cid : stakes) {
            const Corp& t = w.corps[cid];
            int64_t held = c.shares[cid];
            double pct = 100.0 * held / std::max<int64_t>(1, t.sharesOut);
            ImGui::TableNextRow();
            ImGui::PushID(cid);
            ImGui::TableNextColumn(); ImGui::Text("%s  [%s]", t.name.c_str(), t.ticker.c_str());
            ImGui::TableNextColumn(); cellInt(held);
            ImGui::TableNextColumn();
            char b[16];
            std::snprintf(b, sizeof b, "%.1f%%", pct);
            rightText(b, pct > 50 ? col::Good : col::Text);
            ImGui::TableNextColumn(); rightText(fmtMoneyShort(t.netWorth, false));
            ImGui::TableNextColumn(); rightText(fmtMoneyShort((t.sharesOut - held) * t.sharePrice, false));
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(pct <= 50);
            if (ImGui::SmallButton("Take over")) { Command cm; cm.type = Cmd::Merge; cm.a = cid; run(cm); }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// One line on what an event does, for the Events tab.
static std::string eventEffect(const World& w, const GalaxyEvent& e) {
    switch (e.kind) {
        case EventKind::Shortage: return item(e.item).name + " demand x" + fmtQty(e.mag) + " at " + w.stations[e.station].name;
        case EventKind::Glut: return item(e.item).name + " stock flooded, demand x0.25 at " + w.stations[e.station].name;
        case EventKind::Strike: return "no production at " + w.stations[e.station].name;
        case EventKind::Boom: return "Luxury Goods and Electronics demand x" + fmtQty(e.mag) + " across " + w.regions[e.region].name;
        case EventKind::Plague: return "Medical Kits demand x" + fmtQty(e.mag) + ", Ration Packs x1.4 across " + w.regions[e.region].name;
        case EventKind::Levy: return e.mag > 0.01f ? "Charter levy " + fmtQty(e.mag * 100) + "% on sales in Charter space" : "no Charter levy";
        case EventKind::Embargo: return item(e.item).name + " cannot be traded in Charter space; smugglers pay 60% more elsewhere";
        case EventKind::Storm: return "lanes into and out of " + w.systems[e.system].name + " closed";
        case EventKind::PirateSurge: return "raiders gather and the risk ratings climb across " + w.regions[e.region].name;
        case EventKind::GoldRush: return "a rich " + item(e.item).name + " belt in " + w.systems[e.system].name + ", open to all";
        case EventKind::Crackdown: return "Wardens answer in round 1, Charter bounties doubled across " + w.regions[e.region].name;
        default: break;
    }
    return "";
}

void App::winNews() {
    if (!beginWindow(*this, W_NEWS, ImVec2(220, 140), ImVec2(760, 520))) { ImGui::End(); return; }
    if (!ImGui::BeginTabBar("newstabs")) { ImGui::End(); return; }
    auto flags = [&](int i) { return newsTabReq == i ? ImGuiTabItemFlags_SetSelected : 0; };
    if (ImGui::BeginTabItem("Wire", nullptr, flags(0))) {
        static char filt[64] = "";
        static bool onlyMine = false;
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##nf", "search the wire", filt, sizeof filt);
        ImGui::SameLine();
        ImGui::Checkbox("Only stories about me", &onlyMine);
        std::string f = upper(filt);
        std::vector<int> rows;
        for (int i = (int)world.log.size() - 1; i >= 0; i--) {
            const LogEntry& e = world.log[i];
            if (e.chan != Chan::News) continue;
            if (onlyMine && !e.mine) continue;
            if (!f.empty() && upper(e.text).find(f) == std::string::npos) continue;
            rows.push_back(i);
        }
        ImGui::BeginChild("wire", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const LogEntry& e = world.log[rows[r]];
                ImGui::PushFont(fonts.mono, 14.0f);
                textColored(col::Dim, "%s", fmtDate(e.t).c_str());
                ImGui::PopFont();
                ImGui::SameLine();
                if (e.system != NONE) {
                    ImGui::PushID(rows[r]);
                    ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(world.systems[e.system].zone));
                    if (ImGui::SmallButton(world.systems[e.system].name.c_str())) { selectSystem(e.system); centerMap(e.system); }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                    ImGui::SameLine();
                }
                ImU32 c = e.sev == Sev::Bad ? col::Bad : e.sev == Sev::Warn ? col::Warn : e.sev == Sev::Good ? col::Good : col::Text;
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextWrapped("%s", e.text.c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
            }
        if (rows.empty()) textDim("Nothing on the wire yet.");
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Events", nullptr, flags(1))) {
        World& w = world;
        ImGui::PushTextWrapPos(0);
        textDim("What is happening to the galaxy right now, and what is coming. Shortages and outbreaks lift prices; strikes cut supply; "
                "storms close lanes; edicts change the rules in Charter space.");
        if (beginTable("events", 5, 0)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Event");
            ImGui::TableSetupColumn("Where");
            ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Starts");
            ImGui::TableSetupColumn("Ends");
            ImGui::TableHeadersRow();
            Id homeSys = w.stations[w.pc().home].system;
            std::vector<const GalaxyEvent*> list;
            for (auto& e : w.events)
                if (!e.over) list.push_back(&e);
            auto where = [&](const GalaxyEvent* e) { return e->system != NONE ? e->system : e->region != NONE && !w.regionSystems[e->region].empty() ? w.regionSystems[e->region][0] : NONE; };
            std::sort(list.begin(), list.end(), [&](const GalaxyEvent* a, const GalaxyEvent* b) {
                Id wa = where(a), wb = where(b);
                int da = wa == NONE ? -1 : w.jumps(homeSys, wa), db = wb == NONE ? -1 : w.jumps(homeSys, wb);
                return da != db ? da < db : a->id < b->id;
            });
            for (const GalaxyEvent* e : list) {
                ImGui::TableNextRow();
                ImGui::PushID(e->id);
                ImGui::TableNextColumn();
                bool bad = e->kind == EventKind::Storm || e->kind == EventKind::PirateSurge || e->kind == EventKind::Strike || e->kind == EventKind::Embargo;
                textColored(e->applied ? (bad ? col::Warn : col::Accent) : col::Dim, "%s", eventKindName(e->kind));
                ImGui::TableNextColumn();
                Id at = where(e);
                std::string place = e->station != NONE ? w.stations[e->station].name : e->system != NONE ? w.systems[e->system].name
                                    : e->region != NONE ? w.regions[e->region].name : "Charter space";
                if (at != NONE) {
                    if (ImGui::SmallButton(place.c_str())) { selectSystem(at); centerMap(at); }
                } else ImGui::TextUnformatted(place.c_str());
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", eventEffect(w, *e).c_str());
                ImGui::TableNextColumn(); textDim("%s", e->applied ? "under way" : ("in " + fmtDuration(e->start - w.now)).c_str());
                ImGui::TableNextColumn(); textDim("in %s", fmtDuration(e->end - w.now).c_str());
                ImGui::PopID();
            }
            if (list.empty()) { ImGui::TableNextRow(); ImGui::TableNextColumn(); textDim("All quiet."); }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTabItem();
    }
    newsTabReq = -1;
    ImGui::EndTabBar();
    ImGui::End();
}

}  // namespace nb
