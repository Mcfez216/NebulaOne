// NebulaOne - the Colonies window: your colonies, and the planets you could settle.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

static ImU32 planetColor(PlanetType t) {
    static const ImU32 c[PLANET_TYPE_COUNT] = {IM_COL32(120, 200, 120, 255), IM_COL32(90, 150, 230, 255), IM_COL32(170, 150, 120, 255),
                                               IM_COL32(190, 225, 240, 255), IM_COL32(230, 110, 70, 255),  IM_COL32(150, 150, 150, 255),
                                               IM_COL32(220, 180, 120, 255)};
    return c[(int)t];
}

static void planetDot(PlanetType t, int size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 3.0f + size * 1.3f, h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 9, p.y + h * 0.5f), r, planetColor(t), 16);
    ImGui::Dummy(ImVec2(20, h));
    ImGui::SameLine();
}

static void colonyPanel(App& app, Colony& c) {
    World& w = app.world;
    const Station& st = w.stations[c.station];
    std::vector<Planet> ps = w.planetsOf(c.system);
    const Planet& p = ps[c.planet];
    ImGui::PushFont(app.fonts.title, 20.0f);
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    textDim("%s  ·  size %d  ·  habitability %d%%  ·  richness %.2f  ·  %s", planetTypeName(c.type), c.size, (int)std::round(c.hab * 100), c.richness,
            zoneName(w.systems[c.system].zone));
    if (ImGui::BeginTable("ck", 5, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("People", fmtQty(std::round(c.pop * 1000)));
        ImGui::TableNextColumn(); kpi("Room for", fmtQty(c.cap() * 1000));
        ImGui::TableNextColumn(); kpi("Needs met", std::to_string((int)std::round(c.sat * 100)) + "%", c.sat < 0.4f ? col::Bad : c.sat < 0.7f ? col::Warn : col::Good);
        ImGui::TableNextColumn(); kpi("Taxes paid", fmtMoneyShort(c.taxes));
        ImGui::TableNextColumn(); kpi("Goods to you", fmtMoneyShort(c.output));
        ImGui::EndTable();
    }
    if (c.unrest >= 7) textColored(col::Bad, "Unrest: %d days of shortages. %s", (int)c.unrest, c.unrest >= 14 ? "The colony has stopped paying." : "Taxes stop at 14.");
    if (c.popHist.size() >= 2 && ImPlot::BeginPlot("##pop", ImVec2(-1, 110), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        std::vector<double> y;
        for (float v : c.popHist) y.push_back(v);
        ImPlot::SetupAxes(nullptr, "thousand", ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("people", y.data(), (int)y.size(), 1.0, 0.0, {ImPlotProp_LineColor, col::v4(col::Accent)});
        ImPlot::EndPlot();
    }
    header("What its people need");
    if (beginTable("cneeds", 5, 0, ImVec2(0, 34.0f + 26.0f * 5))) {
        ImGui::TableSetupColumn("Good", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Per day");
        ImGui::TableSetupColumn("In stock");
        ImGui::TableSetupColumn("Met");
        ImGui::TableSetupColumn("In your hangar");
        ImGui::TableHeadersRow();
        for (auto& [it, perDay] : w.colonyNeeds(c)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(item(it).name.c_str());
            ImGui::TableNextColumn(); cellQty(perDay);
            ImGui::TableNextColumn(); cellQty(st.stock[it]);
            ImGui::TableNextColumn();
            float r = std::clamp(st.stock[it] / std::max(1.0f, st.target[it]), 0.0f, 1.0f);
            progressBar(r, ImVec2(80, 0), r < 0.4f ? col::Bad : r < 0.7f ? col::Warn : col::Good);
            ImGui::TableNextColumn(); cellInt(w.have(w.player, c.station, it));
        }
        ImGui::EndTable();
    }
    if (accentButton("Hand over supplies from your hangar")) {
        Command cm; cm.type = Cmd::SupplyColony; cm.a = c.id;
        app.run(cm);
    }
    ImGui::SameLine();
    if (ImGui::Button("Spaceport market")) app.showMarket(c.station, RATIONS, 0);
    ImGui::SameLine();
    textDim("Traders sell here when prices rise; a Haulage division can keep it supplied.");
    header("What it makes");
    double value = c.richness * (80000.0 + 5000.0 * c.pop) * (1.0 + 0.5 * c.cmod[CMOD_EXTRACTOR]) * (0.5 + 0.5 * c.sat);
    for (auto& [it, share] : planetOutputs(c.type)) {
        ImGui::Text("%s", item(it).name.c_str());
        ImGui::SameLine(180);
        textDim("about %s a day to your hangar there  (you hold %s)", fmtInt((int64_t)(value * share / toCr(item(it).base))).c_str(),
                fmtInt(w.have(w.player, c.station, it)).c_str());
    }
    header("Development");
    const Project* running = nullptr;
    for (auto& pj : w.projects)
        if (pj.kind == ProjectKind::ColonyUpgrade && pj.station == c.station && pj.stage <= ProjectStage::Building) running = &pj;
    for (int m = 0; m < CMOD_COUNT; m++) {
        ImGui::PushID(m);
        ImGui::Text("%s", cmodName(m));
        ImGui::SameLine(130);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 q = ImGui::GetCursorScreenPos();
        for (int t = 0; t < 3; t++) {
            ImVec2 a(q.x + t * 16, q.y + 3), b(a.x + 12, a.y + 12);
            if (t < c.cmod[m]) dl->AddRectFilled(a, b, col::Accent, 2);
            else dl->AddRect(a, b, col::Border, 2);
        }
        ImGui::Dummy(ImVec2(52, 16));
        ImGui::SameLine();
        textDim("%s", cmodEffect(m));
        ImGui::SameLine();
        if (c.cmod[m] < 3 && !running) {
            if (ImGui::SmallButton("Upgrade")) {
                Command cm; cm.type = Cmd::UpgradeColony; cm.a = c.id; cm.b = m;
                app.run(cm);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                for (auto& [it, n] : w.projectNeeds(ProjectKind::ColonyUpgrade, m, c.cmod[m] + 1))
                    ImGui::Text("%s x %s  (you have %s there)", item(it).name.c_str(), fmtInt(n).c_str(), fmtInt(w.have(w.player, c.station, it)).c_str());
                ImGui::Text("%s and %d days", fmtMoneyShort(w.projectCash(ProjectKind::ColonyUpgrade, m, c.cmod[m] + 1)).c_str(),
                            w.projectDays(ProjectKind::ColonyUpgrade, c.cmod[m] + 1));
                ImGui::EndTooltip();
            }
        } else if (running && running->module == m) {
            textColored(col::Warn, running->stage == ProjectStage::Building ? "building, %s" : "waiting for materials (%s)",
                        running->stage == ProjectStage::Building ? fmtDuration(running->buildEnd - w.now).c_str()
                                                                  : (std::to_string((int)(running->progress() * 100)) + "%").c_str());
        }
        ImGui::PopID();
    }
    if (running && running->stage == ProjectStage::Funding && ImGui::SmallButton("Deliver what is in the spaceport hangar")) {
        Command cm; cm.type = Cmd::ContributeProject; cm.a = running->id; cm.flag = true;
        app.run(cm);
    }
}

static void planetsPanel(App& app) {
    World& w = app.world;
    static Id sys = NONE;
    if (sys == NONE) sys = app.selSystem != NONE ? app.selSystem : w.stations[w.pc().home].system;
    systemPicker(app, "##psys", sys, 260);
    ImGui::SameLine();
    if (ImGui::SmallButton("Selected on the chart") && app.selSystem != NONE) sys = app.selSystem;
    const StarSystem& s = w.systems[sys];
    ImGui::SameLine();
    zoneBadge(s.zone);
    bool charted = w.planetsCharted(sys);
    if (!charted) textDim("Not surveyed: habitability and richness are estimates until a fleet surveys the system.");
    // fleets of yours here with a Colony Kit aboard
    std::vector<Id> carriers;
    for (Id fid : w.pc().fleetList) {
        const Fleet& f = w.fleets[fid];
        Id at = f.docked != NONE ? w.stations[f.docked].system : f.system;
        if (f.alive && at == sys && f.act != Act::Jump && f.cargo.count(COLONY_KIT)) carriers.push_back(fid);
    }
    std::vector<Planet> ps = w.planetsOf(sys);
    if (beginTable("planets", 7, 0, ImVec2(0, 34.0f + 26.0f * ps.size()))) {
        ImGui::TableSetupColumn("Planet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("World");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Habitable");
        ImGui::TableSetupColumn("Richness");
        ImGui::TableSetupColumn("Makes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (auto& p : ps) {
            ImGui::TableNextRow();
            ImGui::PushID(p.index);
            ImGui::TableNextColumn(); planetDot(p.type, p.size); ImGui::TextUnformatted(p.name.c_str());
            ImGui::TableNextColumn(); textColored(planetColor(p.type), "%s", planetTypeName(p.type));
            ImGui::TableNextColumn(); cellInt(p.size);
            ImGui::TableNextColumn();
            if (charted) rightText(std::to_string((int)std::round(p.hab * 100)) + "%");
            else rightText("?", col::Dim);
            ImGui::TableNextColumn();
            if (charted) rightText(fmtQty(p.richness));
            else rightText("?", col::Dim);
            ImGui::TableNextColumn();
            std::string mk;
            for (auto& [it, sh] : planetOutputs(p.type)) mk += (mk.empty() ? "" : ", ") + item(it).name;
            textDim("%s", mk.c_str());
            ImGui::TableNextColumn();
            if (Colony* c = w.colonyOn(sys, p.index)) textColored(c->owner == w.player ? col::Accent : col::Warn, "%s colony", w.corps[c->owner].ticker.c_str());
            else if (s.zone == Zone::Charter) textDim("Charter world");
            else if (!carriers.empty() && ImGui::SmallButton("Settle")) {
                Command cm; cm.type = Cmd::SettleColony; cm.a = carriers[0]; cm.b = p.index;
                app.run(cm);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (carriers.empty() && s.zone != Zone::Charter)
        textDim("To settle here, bring a fleet carrying a Colony Kit (20,000 m3: a Carrack's hold) into this system.");
    // the best charted worlds nearby
    header("The best worlds you have charted nearby");
    struct Row { double score; Id sys; Planet p; int jumps; };
    std::vector<Row> rows;
    std::vector<int> d;
    Id homeSys = w.stations[w.pc().home].system;
    std::vector<Id> near = w.systemsWithin(homeSys, 10, &d);
    for (size_t k = 0; k < near.size(); k++) {
        if (w.systems[near[k]].zone == Zone::Charter || !w.planetsCharted(near[k])) continue;
        for (auto& p : w.planetsOf(near[k]))
            if (!w.colonyOn(near[k], p.index)) rows.push_back({p.hab * p.size * p.richness, near[k], p, d[k]});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
    if (rows.empty()) textDim("None yet: survey Contract and Frontier systems (Survey order) to chart their planets.");
    for (size_t i = 0; i < rows.size() && i < 8; i++) {
        const Row& r = rows[i];
        ImGui::PushID((int)i);
        planetDot(r.p.type, r.p.size);
        if (ImGui::Selectable(r.p.name.c_str(), false, 0, ImVec2(200, 0))) { sys = r.sys; app.centerMap(r.sys); }
        ImGui::SameLine(240);
        textDim("%s, size %d, %d%% habitable, richness %.2f  ·  %d jumps", planetTypeName(r.p.type), r.p.size, (int)std::round(r.p.hab * 100),
                r.p.richness, r.jumps);
        ImGui::PopID();
    }
}

void App::winColonies() {
    if (!beginWindow(*this, W_COLONY, ImVec2(260, 120), ImVec2(820, 640))) { ImGui::End(); return; }
    World& w = world;
    if (ImGui::BeginTabBar("coltabs")) {
        if (ImGui::BeginTabItem("Your colonies")) {
            std::vector<Id> mine;
            for (auto& c : w.colonies)
                if (c.owner == w.player && !w.stations[c.station].dead) mine.push_back(c.id);
            if (mine.empty()) {
                ImGui::PushTextWrapPos(0);
                textDim("No colonies yet. Buy a Colony Kit at a Charter trade hub (or build one), carry it to a planet in Contract space or "
                        "the Frontier, and settle it from the Planets tab. Garden and ocean worlds grow big; rocky, volcanic and barren "
                        "worlds stay small but mine minerals; ice and gas giants make fuel.");
                ImGui::PopTextWrapPos();
            } else {
                static Id sel = NONE;
                if (std::find(mine.begin(), mine.end(), sel) == mine.end()) sel = mine[0];
                if (beginTable("cols", 6, 0, ImVec2(0, std::min(180.0f, 34.0f + 26.0f * mine.size())))) {
                    ImGui::TableSetupColumn("Colony", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("World");
                    ImGui::TableSetupColumn("People");
                    ImGui::TableSetupColumn("Needs met");
                    ImGui::TableSetupColumn("Taxes");
                    ImGui::TableSetupColumn("Goods to you");
                    ImGui::TableHeadersRow();
                    for (Id id : mine) {
                        const Colony& c = w.colonies[id];
                        ImGui::TableNextRow();
                        ImGui::PushID(id);
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(w.stations[c.station].name.c_str(), sel == id, ImGuiSelectableFlags_SpanAllColumns)) {
                            sel = id;
                            centerMap(c.system);
                        }
                        ImGui::TableNextColumn(); textColored(planetColor(c.type), "%s", planetTypeName(c.type));
                        ImGui::TableNextColumn(); rightText(fmtQty(std::round(c.pop * 1000)));
                        ImGui::TableNextColumn();
                        rightText(std::to_string((int)std::round(c.sat * 100)) + "%", c.sat < 0.4f ? col::Bad : c.sat < 0.7f ? col::Warn : col::Good);
                        ImGui::TableNextColumn(); rightText(fmtMoneyShort(c.taxes, false));
                        ImGui::TableNextColumn(); rightText(fmtMoneyShort(c.output, false));
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::BeginChild("colbody");
                colonyPanel(*this, w.colonies[sel]);
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Planets")) { planetsPanel(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Rivals' colonies")) {
            int n = 0;
            for (auto& c : w.colonies) {
                if (c.owner == w.player || w.stations[c.station].dead) continue;
                n++;
                ImGui::PushID(c.id);
                planetDot(c.type, c.size);
                if (ImGui::Selectable(w.stations[c.station].name.c_str(), false, 0, ImVec2(280, 0))) { selectSystem(c.system); centerMap(c.system); }
                ImGui::SameLine(320);
                textDim("%s  ·  %s people  ·  needs met %d%%", w.corps[c.owner].name.c_str(), fmtQty(std::round(c.pop * 1000)).c_str(), (int)std::round(c.sat * 100));
                ImGui::PopID();
            }
            if (!n) textDim("No rival colonies yet.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace nb
