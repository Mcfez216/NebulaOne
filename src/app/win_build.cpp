// NebulaOne - the Construction window: outpost upgrades and megaprojects.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static const char* projectBlurb(ProjectKind k) {
    switch (k) {
        case ProjectKind::JumpGate: return "A permanent two-hex lane between two systems up to 40 hexes apart. Everyone can use it; non-backers pay 20k cr a ship, split among the backers by share.";
        case ProjectKind::Beacon: return "Extends the Charter's Wardens to a Contract system and its Contract neighbours: they answer violence there, and the risk rating falls. The Charter pays 40% of the credits.";
        case ProjectKind::Nexus: return "Turns one of your outposts into a trade hub: a market twice as deep, job seekers, and traders' attention. Needs a tier 2 Market Exchange.";
        case ProjectKind::SurveyArray: return "Live sensors three jumps around the system for the founder, and every hidden site within four jumps found each fortnight.";
        default: return "";
    }
}

// The bill for a project, with what is already in and what waits at the yard.
static void needsTable(App& app, const Project& p) {
    World& w = app.world;
    if (beginTable("needs", 4, 0, ImVec2(0, 34.0f + 26.0f * (p.need.size() + 1)))) {
        ImGui::TableSetupColumn("Needs", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Delivered");
        ImGui::TableSetupColumn("Of");
        ImGui::TableSetupColumn("Yours at the yard");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < p.need.size(); i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(item(p.need[i].first).name.c_str());
            ImGui::TableNextColumn();
            rightText(fmtInt(p.got[i]), p.got[i] >= p.need[i].second ? col::Good : col::Text);
            ImGui::TableNextColumn(); cellInt(p.need[i].second);
            ImGui::TableNextColumn();
            int64_t h = p.yard != NONE ? w.have(w.player, p.yard, p.need[i].first) : 0;
            rightText(fmtInt(h), h > 0 ? col::Accent : col::Dim);
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Credits");
        ImGui::TableNextColumn(); rightText(fmtMoneyShort(p.cashGot, false), p.cashGot >= p.cash ? col::Good : col::Text);
        ImGui::TableNextColumn(); rightText(fmtMoneyShort(p.cash, false));
        ImGui::TableNextColumn(); rightText(fmtMoneyShort(w.pc().wallet, false), col::Dim);
        ImGui::EndTable();
    }
}

static void contributeBar(App& app, const Project& p) {
    World& w = app.world;
    if (p.stage != ProjectStage::Funding) return;
    ImGui::PushID(p.id);
    if (accentButton("Deliver what is at the yard")) {
        Command cm; cm.type = Cmd::ContributeProject; cm.a = p.id; cm.flag = true;
        app.run(cm);
    }
    if (p.yard != NONE && ImGui::IsItemHovered()) ImGui::SetTooltip("Takes the materials it still needs from your hangar at %s.", w.stations[p.yard].name.c_str());
    ImGui::SameLine();
    static Money amt = 0;
    Money left = p.cash - p.cashGot;
    if (amt <= 0 || amt > left) amt = std::min(left, w.pc().wallet);
    inputCredits("##pay", amt, 150);
    ImGui::SameLine();
    if (ImGui::Button("Pay in")) {
        Command cm; cm.type = Cmd::ContributeProject; cm.a = p.id; cm.price = amt; cm.flag = false;
        app.run(cm);
    }
    ImGui::SameLine();
    textDim("A Haulage division with its base where the materials are carries them over for you.");
    ImGui::PopID();
}

static void outpostsTab(App& app) {
    World& w = app.world;
    std::vector<Id> mine;
    for (Id o : w.outpostIds)
        if (!w.stations[o].dead && w.stations[o].owner == w.player) mine.push_back(o);
    if (mine.empty()) {
        ImGui::PushTextWrapPos(0);
        textDim("You have no outposts. Carry an Outpost Kit into an empty Contract system or a Frontier system and anchor it from the "
                "Fleets window. Each outpost can then be upgraded here, module by module, up to tier 3.");
        ImGui::PopTextWrapPos();
        return;
    }
    static Id sel = NONE;
    if (std::find(mine.begin(), mine.end(), sel) == mine.end()) sel = mine[0];
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##outpost", w.stations[sel].name.c_str())) {
        for (Id o : mine) {
            ImGui::PushID(o);
            if (ImGui::Selectable(w.stations[o].name.c_str(), sel == o)) sel = o;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const Station& o = w.stations[sel];
    ImGui::SameLine();
    textColored(o.state == OutpostState::Online ? col::Good : col::Warn, "%s", outpostStateName(o.state));
    ImGui::SameLine();
    textDim("%s  ·  %s%s", w.systems[o.system].name.c_str(), zoneName(w.systems[o.system].zone), o.nexus ? "  ·  Trade Nexus" : "");
    const Project* running = nullptr;
    for (auto& p : w.projects)
        if (p.kind == ProjectKind::Upgrade && p.station == sel && p.stage <= ProjectStage::Building) running = &p;
    header("Modules");
    if (beginTable("smods", 4, 0, ImVec2(0, 34.0f + 30.0f * (float)(int)SMOD_COUNT))) {
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("Tier");
        ImGui::TableSetupColumn("Each tier gives", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int m = 0; m < SMOD_COUNT; m++) {
            ImGui::TableNextRow();
            ImGui::PushID(m);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(smodName(m));
            ImGui::TableNextColumn();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 c = ImGui::GetCursorScreenPos();
            for (int t = 0; t < SMOD_MAX_TIER; t++) {
                ImVec2 a(c.x + t * 16, c.y + 4), b(a.x + 12, a.y + 12);
                if (t < o.smod[m]) dl->AddRectFilled(a, b, col::Accent, 2);
                else dl->AddRect(a, b, col::Border, 2);
            }
            ImGui::Dummy(ImVec2(16 * SMOD_MAX_TIER, 18));
            ImGui::TableNextColumn(); textDim("%s", smodEffect(m));
            ImGui::TableNextColumn();
            if (o.smod[m] >= SMOD_MAX_TIER) textDim("top tier");
            else if (running && running->module == m) textColored(col::Warn, running->stage == ProjectStage::Building ? "building" : "funding");
            else {
                ImGui::BeginDisabled(running != nullptr);
                char lbl[32];
                std::snprintf(lbl, sizeof lbl, "To tier %d", o.smod[m] + 1);
                if (ImGui::SmallButton(lbl)) {
                    Command cm; cm.type = Cmd::UpgradeStation; cm.a = sel; cm.b = m;
                    app.run(cm);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    for (auto& [it, q] : w.projectNeeds(ProjectKind::Upgrade, m, o.smod[m] + 1))
                        ImGui::Text("%s x %s  (you have %s here)", item(it).name.c_str(), fmtInt(q).c_str(), fmtInt(w.have(w.player, sel, it)).c_str());
                    ImGui::Text("%s and %d days", fmtMoneyShort(w.projectCash(ProjectKind::Upgrade, m, o.smod[m] + 1)).c_str(),
                                w.projectDays(ProjectKind::Upgrade, o.smod[m] + 1));
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (running) {
        header("Under way");
        ImGui::Text("%s", running->name.c_str());
        if (running->stage == ProjectStage::Building) {
            float k = 1.0f - (float)(running->buildEnd - w.now) / (float)std::max<Tick>(1, (Tick)running->buildDays * MINUTES_PER_DAY);
            progressBar(k, ImVec2(300, 0), col::Good);
            ImGui::SameLine();
            textDim("ready in %s", fmtDuration(running->buildEnd - w.now).c_str());
        } else {
            progressBar((float)running->progress(), ImVec2(300, 0), col::Warn);
            ImGui::SameLine();
            textDim("%d%% funded: the materials go into the outpost's own hangar", (int)std::round(running->progress() * 100));
            needsTable(app, *running);
            contributeBar(app, *running);
        }
    }
}

static void projectsTab(App& app) {
    World& w = app.world;
    std::vector<Id> rows;
    for (auto& p : w.projects)
        if (p.kind != ProjectKind::Upgrade && p.stage != ProjectStage::Cancelled) rows.push_back(p.id);
    static Id sel = NONE;
    if (rows.empty()) { textDim("No megaprojects yet. Found one on the next tab, or wait for a rival to."); return; }
    if (beginTable("projs", 7, 0, ImVec2(0, std::min(260.0f, 34.0f + 26.0f * rows.size())))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Founder");
        ImGui::TableSetupColumn("Jumps");
        ImGui::TableSetupColumn("Progress");
        ImGui::TableSetupColumn("Your share");
        ImGui::TableSetupColumn("Backers");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        Id homeSys = w.stations[w.pc().home].system;
        const auto& row = w.jumpsFrom(homeSys);
        for (Id id : rows) {
            const Project& p = w.projects[id];
            ImGui::TableNextRow();
            ImGui::PushID(id);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(p.name.c_str(), sel == id, ImGuiSelectableFlags_SpanAllColumns)) { sel = id; app.selectSystem(p.system, false); app.centerMap(p.system); }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(w.corps[p.founder].ticker.c_str());
            ImGui::TableNextColumn(); cellInt(row[p.system]);
            ImGui::TableNextColumn();
            progressBar(p.stage >= ProjectStage::Building ? 1.0f : (float)p.progress(), ImVec2(90, 0),
                        p.stage == ProjectStage::Done ? col::Good : p.stage == ProjectStage::Building ? col::Info : col::Warn);
            ImGui::TableNextColumn();
            Money total = 0, mine = 0;
            for (auto& [cid, v] : p.share) { total += v; if (cid == w.player) mine = v; }
            if (mine > 0) rightText(std::to_string((int)std::round(100.0 * mine / std::max<Money>(1, total))) + "%", col::Accent);
            else textDim("-");
            ImGui::TableNextColumn(); cellInt((int64_t)p.share.size());
            ImGui::TableNextColumn();
            if (p.stage == ProjectStage::Done) textColored(col::Good, p.kind == ProjectKind::JumpGate ? "open" : "complete");
            else if (p.stage == ProjectStage::Building) textColored(col::Info, "building, %s", fmtDuration(p.buildEnd - w.now).c_str());
            else textDim("funding");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (sel == NONE || sel >= (Id)w.projects.size()) return;
    const Project& p = w.projects[sel];
    header(p.name.c_str());
    ImGui::PushTextWrapPos(0);
    textDim("%s", projectBlurb(p.kind));
    ImGui::PopTextWrapPos();
    if (p.yard != NONE) textDim("Yard: %s  ·  founded %s by %s", w.stations[p.yard].name.c_str(), fmtDate(p.founded).c_str(), w.corps[p.founder].name.c_str());
    if (p.kind == ProjectKind::JumpGate && p.stage == ProjectStage::Done) textColored(col::Good, "Tolls collected: %s", fmtMoneyShort(p.tolls).c_str());
    if (p.stage == ProjectStage::Funding) {
        needsTable(app, p);
        contributeBar(app, p);
    }
}

static void foundTab(App& app) {
    World& w = app.world;
    static int kind = 0;  // index into the four megaprojects
    static Id sys = NONE, target = NONE, station = NONE;
    const ProjectKind kinds[] = {ProjectKind::JumpGate, ProjectKind::Beacon, ProjectKind::Nexus, ProjectKind::SurveyArray};
    const char* names[] = {"Jump Gate", "Charter Beacon", "Trade Nexus", "Deep Survey Array"};
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("Project", &kind, names, 4);
    ProjectKind k = kinds[kind];
    ImGui::PushTextWrapPos(0);
    textDim("%s", projectBlurb(k));
    ImGui::PopTextWrapPos();
    if (sys == NONE) sys = app.selSystem != NONE ? app.selSystem : w.stations[w.pc().home].system;
    if (k == ProjectKind::Nexus) {
        textDim("Outpost");
        ImGui::SameLine(110);
        stationPicker(app, "##nx", station, 300, [&](const Station& s) { return s.isOutpost() && s.owner == w.player && !s.dead; });
    } else {
        textDim(k == ProjectKind::JumpGate ? "From" : "System");
        ImGui::SameLine(110);
        systemPicker(app, "##psys", sys, 260);
        ImGui::SameLine();
        if (ImGui::SmallButton("Selected on the chart") && app.selSystem != NONE) sys = app.selSystem;
        if (k == ProjectKind::JumpGate) {
            textDim("To");
            ImGui::SameLine(110);
            if (target == NONE) target = sys;
            systemPicker(app, "##ptgt", target, 260);
            if (target != NONE && sys != NONE) {
                ImGui::SameLine();
                textDim("%d hexes, %d jumps today", hexDistance(w.systems[sys].hex, w.systems[target].hex), w.jumps(sys, target));
            }
        }
    }
    std::string why = w.projectWhy(w.player, k, k == ProjectKind::Nexus && station != NONE ? w.stations[station].system : sys, target, station);
    header("The bill");
    for (auto& [it, q] : w.projectNeeds(k, -1, 0)) textDim("%s x %s", item(it).name.c_str(), fmtInt(q).c_str());
    Money cash = w.projectCash(k, -1, 0);
    textDim("%s in credits%s; registration %s now; %d days to build once funded.", fmtMoneyShort(cash).c_str(),
            k == ProjectKind::Beacon ? " (the Charter pays 40%)" : "", fmtMoneyShort(cash / 20).c_str(), w.projectDays(k, 0));
    if (!why.empty()) textColored(col::Warn, "%s", why.c_str());
    ImGui::BeginDisabled(!why.empty());
    if (accentButton("Found the project")) {
        Command cm; cm.type = Cmd::FoundProject; cm.a = (int)k; cm.b = sys; cm.qty = target; cm.item = (int)station;
        app.run(cm);
    }
    ImGui::EndDisabled();
}

void App::winBuild() {
    if (!beginWindow(*this, W_BUILD, ImVec2(240, 110), ImVec2(860, 600))) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("buildtabs")) {
        auto tf = [&](int i) { return buildTabReq == i ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Your outposts", nullptr, tf(0))) { outpostsTab(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Megaprojects", nullptr, tf(1))) { projectsTab(*this); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Found a project", nullptr, tf(2))) { foundTab(*this); ImGui::EndTabItem(); }
        buildTabReq = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace nb
