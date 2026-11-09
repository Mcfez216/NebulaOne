// NebulaOne - Industry: refining, manufacturing, jobs and the blueprint catalogue.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

void App::winIndustry() {
    if (!beginWindow(*this, W_INDUSTRY, ImVec2(130, 90), ImVec2(860, 580))) { ImGui::End(); return; }
    World& w = world;
    if (selStation == NONE) selStation = w.pc().home;
    if (ImGui::BeginTabBar("itabs")) {
        if (ImGui::BeginTabItem("Refine")) {
            stationPicker(*this, "Station##ref", selStation);
            double y = w.refineYield(w.player, selStation);
            ImGui::SameLine();
            const Station& rs = w.stations[selStation];
            textDim("yield %.0f%%  ·  fee %s  ·  ~%d min per job", y * 100,
                    rs.isOutpost() ? (rs.owner == w.player ? "none (your outpost)" : "outpost tax") : "2% of mineral value",
                    (int)std::max(10.0, 60.0 * (1.0 - 0.2 * w.execBonus(w.player, SEAT_INDUSTRY))));
            static int ore = -1;
            static int64_t qty = 0;
            bool any = false;
            if (beginTable("ores", 5, 0, ImVec2(0, 200))) {
                ImGui::TableSetupColumn("Ore", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("In hangar");
                ImGui::TableSetupColumn("Sell raw (best bid)");
                ImGui::TableSetupColumn("Refined value (est.)");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (int o = 0; o < ORE_COUNT; o++) {
                    int64_t q = w.have(w.player, selStation, o);
                    if (q <= 0) continue;
                    any = true;
                    Money raw = w.bestBid(selStation, o) * q;
                    Money ref = 0;
                    for (int m = 0; m < MINERAL_COUNT; m++)
                        ref += (Money)(item(o).refine[m] * (q / 100) * y) * w.refPrice(selStation, MINERAL_FIRST + m);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(o);
                    if (ImGui::Selectable(item(o).name.c_str(), ore == o, ImGuiSelectableFlags_SpanAllColumns)) { ore = o; qty = q / 100 * 100; }
                    ImGui::PopID();
                    ImGui::TableNextColumn(); cellInt(q);
                    ImGui::TableNextColumn(); if (raw) cellMoney(raw); else rightText("no buyer", col::Dim, false);
                    ImGui::TableNextColumn(); rightText(fmtMoney(ref, false), ref > raw ? col::Good : col::Text);
                    ImGui::TableNextColumn();
                    if (ref > raw) textColored(col::Good, "refine"); else textDim("sell raw");
                }
                ImGui::EndTable();
            }
            if (!any) textDim("No ore in this hangar. Mining fleets unload here when you choose this station.");
            if (ore >= 0 && w.have(w.player, selStation, ore) > 0) {
                header("Refine");
                ImGui::Text("%s", item(ore).name.c_str());
                ImGui::SameLine();
                inputQty("units (batches of 100)", qty);
                int64_t batches = qty / 100;
                ImGui::Text("Output:");
                Money fee = 0;
                for (int m = 0; m < MINERAL_COUNT; m++) {
                    int64_t n = (int64_t)std::floor(item(ore).refine[m] * batches * y);
                    if (n <= 0) continue;
                    fee += item(MINERAL_FIRST + m).base * n * 2 / 100;
                    ImGui::SameLine();
                    textColored(col::Accent, "%s %s", fmtInt(n).c_str(), item(MINERAL_FIRST + m).name.c_str());
                }
                textDim("Fee %s", fmtMoney(fee).c_str());
                if (accentButton("Start refining", ImVec2(160, 0))) {
                    Command c; c.type = Cmd::Refine; c.a = selStation; c.item = ore; c.qty = qty;
                    run(c);
                }
                anchor("refine_btn");
            }
            ImGui::EndTabItem();
        }
        // ---- the blueprints you hold here, and what each one needs
        static int bp = -1;
        static int64_t runs = 1;
        auto heldAt = [&](Id st, int b) {
            const BlueprintDef& d = blueprint(b);
            return w.have(w.player, st, d.bpo) + (d.bpc >= 0 ? w.have(w.player, st, d.bpc) : 0);
        };
        auto bpList = [&](const char* id, bool originalsOnly) {
            std::vector<std::pair<Id, int>> owned;  // station, blueprint
            for (auto& [k, q] : w.pc().hangar) {
                int it = (int)(k % HANGAR_KEY);
                if (isBpo(it)) owned.push_back({(Id)(k / HANGAR_KEY), item(it).bp});
                if (!originalsOnly && isBpc(it)) owned.push_back({(Id)(k / HANGAR_KEY), item(it).bp});
            }
            std::sort(owned.begin(), owned.end());
            owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
            if (owned.empty()) {
                textDim("You hold no blueprints yet. Originals are sold at the Charter trade hubs (Blueprints tab); ship copies trade there too.");
                return;
            }
            if (beginTable(id, 5, 0, ImVec2(0, std::min(220.0f, 30.0f + 24.0f * owned.size())))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Blueprint", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("ME");
                ImGui::TableSetupColumn("TE");
                ImGui::TableSetupColumn("Station", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (auto& [st, b] : owned) {
                    const BlueprintDef& d = blueprint(b);
                    int64_t orig = w.have(w.player, st, d.bpo), copies = d.bpc >= 0 ? w.have(w.player, st, d.bpc) : 0;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(st * 64 + b);
                    if (ImGui::Selectable(item(d.product).name.c_str(), bp == b && selStation == st, ImGuiSelectableFlags_SpanAllColumns)) {
                        bp = b;
                        selStation = st;
                    }
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (orig) textColored(col::Accent, "original"); else textDim("%s copies", fmtInt(copies).c_str());
                    ImGui::TableNextColumn(); rightText(std::to_string(w.pc().me[b]));
                    ImGui::TableNextColumn(); rightText(std::to_string(w.pc().te[b]));
                    ImGui::TableNextColumn(); textDim("%s", w.stations[st].name.c_str());
                }
                ImGui::EndTable();
            }
        };
        if (ImGui::BeginTabItem("Manufacture")) {
            bpList("bpl_m", false);
            if (bp >= 0 && heldAt(selStation, bp) > 0) {
                const BlueprintDef& d = blueprint(bp);
                const Corp& c = w.pc();
                header((std::string("Build ") + item(d.product).name + " at " + w.stations[selStation].name).c_str());
                inputQty("Runs", runs, 80);
                if (runs < 1) runs = 1;
                ImGui::SameLine();
                textDim("%d per run  ·  material efficiency %d (-%d%%)  ·  time efficiency %d (-%d%%)", d.perRun, c.me[bp], c.me[bp], c.te[bp],
                        c.te[bp] * 2);
                Money matCost = 0;
                bool ready = true;
                if (beginTable("mats", 6, 0, ImVec2(0, 200))) {
                    ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Needed");
                    ImGui::TableSetupColumn("In hangar");
                    ImGui::TableSetupColumn("Missing");
                    ImGui::TableSetupColumn("Cost here");
                    ImGui::TableSetupColumn("");
                    ImGui::TableHeadersRow();
                    for (auto& [m, q] : d.materials) {
                        int64_t need = matNeed(q, runs, c.me[bp]), have = w.have(w.player, selStation, m);
                        Money ask = w.bestAsk(selStation, m);
                        Money unit = ask ? ask : w.refPrice(selStation, m);
                        matCost += unit * need;
                        ready = ready && have >= need;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(item(m).name.c_str());
                        if (isComponent(m)) { ImGui::SameLine(); textDim("component"); }
                        ImGui::TableNextColumn(); cellInt(need);
                        ImGui::TableNextColumn(); cellInt(have);
                        ImGui::TableNextColumn();
                        if (need > have) rightText(fmtInt(need - have), col::Bad); else rightText("-", col::Good);
                        ImGui::TableNextColumn(); rightText(fmtMoneyShort(unit * need, false));
                        ImGui::TableNextColumn();
                        ImGui::PushID(m);
                        if (need > have && ImGui::SmallButton("Buy")) showMarket(selStation, m, 0);
                        if (need > have && isComponent(m)) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Build")) bp = item(m).bp;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                Money fee = d.materialBase() * runs / 100;
                double mins = d.minutes * (double)runs * (1.0 - 0.02 * c.te[bp]) * (1.0 - 0.2 * w.execBonus(w.player, SEAT_INDUSTRY));
                if (w.stations[selStation].type == StationType::Industrial) mins *= 0.85;
                Money sale = w.refPrice(selStation, d.product) * d.perRun * runs;
                textDim("Install fee %s  ·  build time %s  ·  output worth about %s here", fmtMoney(fee).c_str(),
                        fmtDuration((Tick)mins).c_str(), fmtMoneyShort(sale).c_str());
                if (matCost > 0) {
                    double mg = ((double)sale / (double)(matCost + fee) - 1.0) * 100.0;
                    ImGui::SameLine();
                    textColored(mg > 5 ? col::Good : mg < 0 ? col::Bad : col::Text, " margin %+.0f%%", mg);
                }
                if (w.have(w.player, selStation, d.bpo) <= 0) textColored(col::Warn, "Uses %s copies, one per run.", fmtInt(runs).c_str());
                ImGui::BeginDisabled(!ready);
                if (accentButton("Start manufacturing", ImVec2(190, 0))) {
                    Command cm; cm.type = Cmd::Manufacture; cm.a = selStation; cm.item = bp; cm.qty = runs;
                    run(cm);
                }
                ImGui::EndDisabled();
                if (!ready) { ImGui::SameLine(); textDim("Materials missing."); }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Research")) {
            textDim("Research belongs to the corporation: every original of that blueprint benefits. Research stations work 30% faster.");
            bpList("bpl_r", true);
            if (bp >= 0 && w.have(w.player, selStation, blueprint(bp).bpo) > 0) {
                const BlueprintDef& d = blueprint(bp);
                const Corp& c = w.pc();
                header((item(d.product).name + " Blueprint").c_str());
                static int64_t levels = 1, copies = 5;
                double lab = (w.stations[selStation].type == StationType::Research ? 0.7 : 1.0) * (1.0 - 0.2 * w.execBonus(w.player, SEAT_INDUSTRY));
                auto researchTime = [&](int from, int64_t n) {
                    double m = 0;
                    for (int k = 0; k < n; k++) m += researchMinutes(d, from + k);
                    return (Tick)(m * lab);
                };
                inputQty("Levels", levels, 80);
                levels = std::clamp<int64_t>(levels, 1, 10);
                if (ImGui::BeginTable("res", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    textColored(col::Accent, "MATERIAL EFFICIENCY %d", c.me[bp]);
                    textDim("Each level saves 1%% of every material.");
                    textDim("Next %d: %s, fee %s", (int)levels, fmtDuration(researchTime(c.me[bp], levels)).c_str(),
                            fmtMoneyShort(d.bpoPrice * 2 / 100 * levels).c_str());
                    ImGui::BeginDisabled(c.me[bp] + levels > 10);
                    if (ImGui::Button("Research ME")) { Command cm; cm.type = Cmd::Research; cm.a = selStation; cm.item = bp; cm.qty = levels; cm.flag = false; run(cm); }
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    textColored(col::Accent, "TIME EFFICIENCY %d", c.te[bp]);
                    textDim("Each level saves 2%% of build time.");
                    textDim("Next %d: %s, fee %s", (int)levels, fmtDuration(researchTime(c.te[bp], levels)).c_str(),
                            fmtMoneyShort(d.bpoPrice * 2 / 100 * levels).c_str());
                    ImGui::BeginDisabled(c.te[bp] + levels > 10);
                    if (ImGui::Button("Research TE")) { Command cm; cm.type = Cmd::Research; cm.a = selStation; cm.item = bp; cm.qty = levels; cm.flag = true; run(cm); }
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    if (d.bpc >= 0) {
                        textColored(col::Accent, "COPIES");
                        textDim("One-run copies build a hull each. Sell them, or build in several places at once.");
                        inputQty("Copies", copies, 80);
                        copies = std::max<int64_t>(1, copies);
                        textDim("%s, fee %s", fmtDuration((Tick)(d.minutes * 0.5 * copies * lab)).c_str(), fmtMoneyShort(d.bpoPrice / 200 * copies).c_str());
                        if (ImGui::Button("Copy")) { Command cm; cm.type = Cmd::CopyBlueprint; cm.a = selStation; cm.item = bp; cm.qty = copies; run(cm); }
                        Money px = w.refPrice(selStation, d.bpc);
                        textDim("Copies trade near %s each.", fmtMoneyShort(px).c_str());
                    } else {
                        textDim("Only ship blueprints can be copied.");
                    }
                    ImGui::TableNextColumn();
                    textDim("A blueprint busy with research or copying cannot build at the same time.");
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Jobs")) {
            if (beginTable("jobs", 5)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Station");
                ImGui::TableSetupColumn("Progress");
                ImGui::TableSetupColumn("Ready in");
                ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                int shown = 0;
                for (int i = (int)w.jobs.size() - 1; i >= 0 && shown < 200; i--) {
                    const Job& j = w.jobs[i];
                    if (j.corp != w.player) continue;
                    shown++;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    switch (j.kind) {
                        case JobKind::Refine: ImGui::Text("Refine %s %s", fmtInt(j.runs).c_str(), item(j.item).name.c_str()); break;
                        case JobKind::Manufacture: ImGui::Text("Build %lld x %s", (long long)j.runs, item(blueprint(j.item).product).name.c_str()); break;
                        case JobKind::ResearchME: ImGui::Text("ME research +%lld: %s", (long long)j.runs, item(blueprint(j.item).product).name.c_str()); break;
                        case JobKind::ResearchTE: ImGui::Text("TE research +%lld: %s", (long long)j.runs, item(blueprint(j.item).product).name.c_str()); break;
                        case JobKind::Copy: ImGui::Text("Copy x%lld: %s", (long long)j.runs, item(blueprint(j.item).product).name.c_str()); break;
                    }
                    ImGui::TableNextColumn(); textDim("%s", w.stations[j.station].name.c_str());
                    ImGui::TableNextColumn();
                    float k = j.active ? std::clamp((float)(w.now - j.start) / (float)std::max<Tick>(1, j.end - j.start), 0.0f, 1.0f) : 1.0f;
                    progressBar(k, ImVec2(110, 0), j.active ? col::Accent : col::Good);
                    ImGui::TableNextColumn();
                    if (j.active) monoText(fmtDuration(j.end - w.now)); else textColored(col::Good, "done");
                    ImGui::TableNextColumn();
                    std::string out;
                    for (auto& [it, q] : j.out) out += (out.empty() ? "" : ", ") + fmtInt(q) + " " + item(it).name;
                    textDim("%s", out.c_str());
                }
                ImGui::EndTable();
                if (!shown) textDim("No jobs yet.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blueprints")) {
            // the nearest Charter hub: the Guild sells originals there
            Id homeSys = w.stations[w.pc().home].system;
            const auto& row = w.jumpsFrom(homeSys);
            Id hub = w.pc().home;
            int bd = 1 << 30;
            for (Id h : w.hubs)
                if (w.systems[w.stations[h].system].zone == Zone::Charter && row[w.stations[h].system] < bd) { bd = row[w.stations[h].system]; hub = h; }
            textDim("Originals never run out; the Guild sells them at fixed prices at Charter trade hubs. Prices here are at %s.",
                    w.stations[hub].name.c_str());
            if (beginTable("bps", 7)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Blueprint", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Price");
                ImGui::TableSetupColumn("Time per run");
                ImGui::TableSetupColumn("Materials now");
                ImGui::TableSetupColumn("Output sells for");
                ImGui::TableSetupColumn("Margin");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                const char* group[] = {"COMPONENTS", "SHIP MODULES", "HULLS", "STRUCTURES"};
                for (int b = 0; b < BLUEPRINT_COUNT; b++) {
                    int g = b < BP_MODULE_FIRST ? 0 : b < BP_SHIP_FIRST ? 1 : b < BP_OUTPOST ? 2 : 3;
                    if (b == 0 || b == BP_MODULE_FIRST || b == BP_SHIP_FIRST || b == BP_OUTPOST) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        textColored(col::Accent, "%s", group[g]);
                    }
                    const BlueprintDef& d = blueprint(b);
                    Money mats = 0;
                    for (auto& [m, q] : d.materials) {
                        Money a2 = w.bestAsk(hub, m);
                        mats += (a2 ? a2 : w.refPrice(hub, m)) * matNeed(q, 1, w.pc().me[b]);
                    }
                    Money out = w.refPrice(hub, d.product) * d.perRun;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("  %s", item(d.product).name.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        textDim("%s", item(d.product).desc.c_str());
                        for (auto& [m, q] : d.materials) ImGui::Text("%s %s", fmtInt(q).c_str(), item(m).name.c_str());
                        ImGui::EndTooltip();
                    }
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(d.bpoPrice, false));
                    ImGui::TableNextColumn(); rightText(fmtDuration(d.minutes));
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(mats, false));
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(out, false));
                    ImGui::TableNextColumn();
                    double mg = mats > 0 ? ((double)out / (double)(mats + mats / 100) - 1.0) * 100.0 : 0;
                    char bb[16];
                    std::snprintf(bb, sizeof bb, "%+.0f%%", mg);
                    rightText(bb, mg > 5 ? col::Good : mg < 0 ? col::Bad : col::Text);
                    ImGui::TableNextColumn();
                    ImGui::PushID(b);
                    if (ImGui::SmallButton("Buy original")) showMarket(hub, d.bpo, 0);
                    if (d.bpc >= 0) { ImGui::SameLine(); if (ImGui::SmallButton("Copies")) showMarket(hub, d.bpc, 2); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            textDim("Margins use today's asks for materials. Components are built from minerals; hulls and modules from components.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace nb
