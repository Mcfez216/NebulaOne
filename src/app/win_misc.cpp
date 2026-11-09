// NebulaOne - System information window.
#include <algorithm>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

void App::winSystem() {
    if (!beginWindow(*this, W_SYSTEM, ImVec2(1180, 70), ImVec2(460, 640))) { ImGui::End(); return; }
    World& w = world;
    if (selSystem == NONE) selSystem = w.stations[w.pc().home].system;
    systemPicker(*this, "##sys", selSystem, -1);
    const StarSystem& s = w.systems[selSystem];
    ImGui::PushFont(fonts.title, 23.0f);
    ImGui::TextUnformatted(s.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    monoText(s.hexId, col::Dim);
    zoneBadge(s.zone);
    ImGui::SameLine();
    riskBadge(s.risk, s.riskLive);
    ImGui::SameLine();
    textDim("·  %s  ·  depth %d", w.regions[s.region].name.c_str(), s.depth);
    Id homeSys = w.stations[w.pc().home].system;
    int j = w.jumps(homeSys, selSystem);
    int safe = (int)w.route(homeSys, selSystem, RoutePolicy::LowestRisk).size();
    textDim("%d jumps from home (%d by the lowest-risk route)", j, safe);
    switch (s.zone) {
        case Zone::Charter:
            textColored(zoneColor(s.zone), "Charter space. Wardens answer violence in round %d; aggressors lose standing.", w.wardenRound(selSystem));
            break;
        case Zone::Contract:
            textColored(zoneColor(s.zone), "Contract space. No police: safety is what corporations pay for. Richer ore.");
            break;
        case Zone::Frontier:
            textColored(zoneColor(s.zone), "The Frontier. Unchartered: no stations but outposts, the richest rock.");
            break;
    }
    if (s.owner != NONE) textColored(s.owner == w.player ? col::Accent : col::Warn, "Sovereignty: %s holds an outpost here.", w.corps[s.owner].name.c_str());
    if (w.stormBlocked(selSystem)) textColored(col::Warn, "Ion storm: every lane in and out is closed for another %s.", fmtDuration(w.stormUntil[selSystem] - w.now).c_str());
    for (auto& e : w.events) {
        if (e.over || e.kind == EventKind::Storm) continue;
        bool here = e.system == selSystem || (e.station != NONE && w.stations[e.station].system == selSystem) ||
                    (e.region == s.region && e.station == NONE && e.system == NONE && e.region != NONE);
        if (here) textColored(e.applied ? col::Warn : col::Dim, "%s%s: %s.", e.applied ? "" : "Coming: ", eventKindName(e.kind), e.headline.c_str());
    }
    if (w.fog) {
        if (w.seen(selSystem)) textColored(col::Good, "Sensors: live. Markets and fleets here are current.");
        else if (w.sighted(selSystem)) textColored(col::Info, "Sensors: broker sightings only. Fleets are shown; markets are not live.");
        else textDim("Sensors: none. Markets show your last snapshot; fleets here are unknown.");
    }
    if (ImGui::SmallButton("Centre map")) centerMap(selSystem);
    ImGui::SameLine();
    if (ImGui::SmallButton("Contracts here")) openWin(W_CONTRACTS);
    ImGui::SameLine();
    if (ImGui::SmallButton("Survey here")) openWin(W_INTEL);
    ImGui::SameLine();
    {
        auto it = w.surveyed.find(selSystem);
        bool done = it != w.surveyed.end() && it->second == w.fortnight();
        textDim(done ? "surveyed this fortnight" : "not surveyed this fortnight");
    }

    header("Underwriters' file");
    if (ImGui::BeginTable("risk", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Published", riskGrade(s.risk), riskColor(s.risk));
        ImGui::TableNextColumn(); kpi("Today", riskGrade(s.riskLive), riskColor(s.riskLive));
        ImGui::TableNextColumn(); kpi("Losses (M cr)", fmtQty(s.losses));
        ImGui::TableNextColumn(); kpi("Heat", fmtQty(s.heat), s.heat > 1 ? col::Bad : col::Text);
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0);
    textDim("Hull cover here: %.1f%% of value per 30 days. Value destroyed and violence push the rating down; it is "
            "republished weekly.", 3.0 + 0.2 * s.risk);
    ImGui::PopTextWrapPos();

    header("Stations");
    if (s.stations.empty()) textDim(s.zone == Zone::Frontier ? "None. Anchor an outpost kit here to claim the system." : "None.");
    for (Id st : s.stations) {
        ImGui::PushID(st);
        const Station& S = w.stations[st];
        if (S.dead) { textDim("%s (destroyed)", S.name.c_str()); ImGui::PopID(); continue; }
        ImGui::TextUnformatted(S.name.c_str());
        ImGui::SameLine();
        textDim("%s", stationTypeName(S.type));
        if (S.isOutpost()) {
            ImGui::SameLine();
            textColored(S.state == OutpostState::Online ? col::Good : col::Warn, "%s", outpostStateName(S.state));
        }
        if (ImGui::SmallButton("Market")) showMarket(st, -1, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("Send fleet here") && selFleet != NONE) {
            Command c; c.type = Cmd::SetFleetOrder; c.a = selFleet; c.order.kind = OrderKind::Move; c.order.station = st;
            run(c);
        }
        if (selFleet == NONE && ImGui::IsItemHovered()) ImGui::SetTooltip("Select one of your fleets first (Fleets window).");
        ImGui::PopID();
    }
    header("Planets");
    {
        std::vector<Planet> ps = w.planetsOf(selSystem);
        bool charted = w.planetsCharted(selSystem);
        std::string line;
        for (auto& p : ps) {
            Colony* c = w.colonyOn(selSystem, p.index);
            ImGui::PushID(p.index + 9000);
            ImGui::Text("%s", p.name.substr(s.name.size() + 1).c_str());
            ImGui::SameLine(60);
            textDim("%s, size %d%s", planetTypeName(p.type), p.size,
                    charted ? (", " + std::to_string((int)std::round(p.hab * 100)) + "% habitable").c_str() : "");
            if (c) {
                ImGui::SameLine();
                textColored(c->owner == w.player ? col::Accent : col::Warn, "  %s colony, %s people", w.corps[c->owner].ticker.c_str(), fmtQty(c->pop * 1000).c_str());
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Colonies...")) openWin(W_COLONY);
    }
    header("Asteroid belts");
    if (s.belts.empty()) textDim("None.");
    for (int b = 0; b < (int)s.belts.size(); b++) {
        const Belt& bl = s.belts[b];
        if (!w.beltVisible(bl, w.player) || (bl.capacity <= 0 && bl.remaining < 1)) continue;
        ImGui::PushID(b + 1000);
        ImGui::Text("Belt %d  %s", b + 1, item(bl.ore).name.c_str());
        if (bl.knownBy == w.player) { ImGui::SameLine(); textColored(col::Accent, "hidden"); }
        else if (bl.until > 0) { ImGui::SameLine(); textColored(col::Warn, "rush"); }
        ImGui::SameLine(170);
        progressBar((float)(bl.remaining / bl.capacity), ImVec2(110, 0));
        ImGui::SameLine();
        textDim("%s", fmtQty(bl.remaining).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Mine")) {
            if (selFleet != NONE) {
                draft.fleet = selFleet;
                draft.o = FleetOrder{};
                draft.o.kind = OrderKind::Mine;
                draft.o.system = selSystem;
                draft.o.belt = b;
                draft.o.station = w.nearestStation(selSystem);
            }
            openWin(W_FLEETS);
            if (selFleet == NONE) toast("Form a fleet first, then choose this belt in its Mine order.", false);
        }
        ImGui::PopID();
    }
    header("Fleets in the system");
    int n = 0;
    auto row = [&](const Fleet& f) {
        n++;
        const Corp& c = w.corps[f.corp];
        bool hostile = c.arch == Arch::Pirate || c.arch == Arch::Ganker || w.isOutlaw(f.corp);
        ImU32 cc = f.corp == w.player ? col::Accent : hostile ? col::Bad : col::Text;
        ImGui::PushID(f.id);
        if (f.corp == w.player) {
            if (ImGui::Selectable(f.name.c_str(), selFleet == f.id)) { selFleet = f.id; openWin(W_FLEETS); }
        } else {
            textColored(cc, "%s", c.name.c_str());
        }
        ImGui::SameLine(200);
        textDim("%d ships  ·  %s  ·  %s", (int)f.ships.size(), f.docked != NONE ? "docked" : actName(f.act),
                f.corp == w.player ? orderKindName(f.order.kind) : archName(c.arch));
        ImGui::PopID();
    };
    for (Id fid : w.pc().fleetList)
        if (w.fleets[fid].alive && w.fleets[fid].system == selSystem) row(w.fleets[fid]);
    if (!w.sighted(selSystem)) { textDim("No sensors here: other fleets are unknown."); n++; }
    else if (selSystem < (Id)w.fleetsIn.size())
        for (Id fid : w.fleetsIn[selSystem])
            if (w.fleets[fid].alive && w.fleets[fid].corp != w.player && w.fleets[fid].system == selSystem) row(w.fleets[fid]);
    if (!n) textDim("Quiet.");
    header("Sites you have found here");
    {
        int ns = 0;
        for (auto& st : w.sites) {
            if (st.finder != w.player || st.done || st.expires <= w.now || (st.system != selSystem && st.link != selSystem)) continue;
            ns++;
            ImGui::PushID(st.id + 7000);
            textColored(col::Accent, "%s", st.name.c_str());
            ImGui::SameLine();
            textDim("%s  ·  closes in %s", siteKindName(st.kind), fmtDuration(st.expires - w.now).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Details")) { selSite = st.id; openWin(W_INTEL); }
            ImGui::PopID();
        }
        if (!ns) textDim("None. Survey the system to look.");
    }
    header("Lanes");
    for (auto& e : w.drift) {
        if (e.corp != w.player || (e.a != selSystem && e.b != selSystem)) continue;
        Id to = e.a == selSystem ? e.b : e.a;
        ImGui::PushID(to + 90000);
        ImGui::PushStyleColor(ImGuiCol_Text, col::Accent);
        if (ImGui::Selectable(w.systems[to].name.c_str(), false, 0, ImVec2(160, 0))) { selSystem = to; centerMap(to); }
        ImGui::PopStyleColor();
        ImGui::SameLine(180);
        textDim("%d hexes", e.hexes);
        ImGui::SameLine(260);
        textColored(col::Accent, "drift lane (yours)");
        ImGui::PopID();
    }
    for (auto& l : s.lanes) {
        const StarSystem& t = w.systems[l.to];
        ImGui::PushID(l.to + 5000);
        ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(t.zone));
        if (ImGui::Selectable(t.name.c_str(), false, 0, ImVec2(160, 0))) { selSystem = l.to; centerMap(l.to); }
        ImGui::PopStyleColor();
        ImGui::SameLine(180);
        textDim("%d hexes", l.hexes);
        ImGui::SameLine(260);
        riskBadge(t.risk, t.riskLive);
        if (t.zone != s.zone) { ImGui::SameLine(); textColored(col::Accent, "border"); }
        ImGui::PopID();
    }
    ImGui::End();
}

}  // namespace nb
