// NebulaOne - the Divisions window: directors, their limits and fleets, the
// monthly reports, staff requests, and the Intelligence division's operations.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"
#include "implot.h"

namespace nb {

static const char* budgetLabel(int d) {
    switch (d) {
        case DIV_TRADE: return "Trading capital (in cargo at once)";
        case DIV_INDUSTRY: return "Spending per 30 days (materials, research)";
        case DIV_HAULAGE: return "Spending per 30 days (fuel)";
        case DIV_INTEL: return "Spending per 30 days (operations; 0 = no cap)";
        default: return nullptr;
    }
}

static const char* divBlurb(int d) {
    switch (d) {
        case DIV_MINING: return "Sends its fleets to the best belts within its limits, refines what they bring in and sells the minerals (or keeps them for Industry).";
        case DIV_TRADE: return "Runs its haulers on the most profitable trade routes it can find within its limits, with its trading capital.";
        case DIV_HAULAGE: return "Keeps your outposts fuelled, takes courier work from the board, and brings stock scattered around the base home.";
        case DIV_INDUSTRY: return "Keeps manufacturing lines busy at its base with the blueprints you own there, buys the materials it lacks, lists what it builds, and researches when a line is free.";
        case DIV_SECURITY: return "Patrols where your miners and haulers work, answers sieges on your outposts first, and can hunt bounties.";
        case DIV_INTEL: return "Runs espionage against rivals when you order it, and counter-intelligence against theirs all the time.";
    }
    return "";
}

// A director's card: who runs it, how good, how happy, and the seat picker.
static void directorCard(App& app, int d) {
    World& w = app.world;
    Corp& c = w.pc();
    int seat = divisionSeat(d);
    Person* p = w.director(w.player, d);
    ImGui::BeginGroup();
    if (!p) {
        textColored(col::Warn, "No director.");
        textDim("Hire an executive in the People window, then put them in this seat.");
    } else {
        ImGui::PushFont(app.fonts.bold, kFontHead);
        ImGui::TextUnformatted(p->name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        textDim("%s%s", seatName(seat), p->seat == seat ? "  ·  a natural fit (+15%)" : "");
        if (ImGui::BeginTable("dcard", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            textDim("SKILL");
            progressBar(p->mgmt / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x - 34, 0), col::Accent, std::to_string(p->mgmt).c_str());
            ImGui::TableNextColumn();
            textDim("LOYALTY");
            progressBar(p->loyalty / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x - 34, 0),
                        p->loyalty < 30 ? col::Bad : p->loyalty < 55 ? col::Warn : col::Good, std::to_string((int)p->loyalty).c_str());
            ImGui::TableNextColumn();
            textDim("AMBITION");
            progressBar(p->ambition / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x - 34, 0), col::Neutral, std::to_string(p->ambition).c_str());
            ImGui::TableNextColumn();
            textDim("WAGE");
            Money fair = w.fairWage(*p);
            textColored(p->wage < fair * 95 / 100 ? col::Warn : col::Text, "%s / day", fmtMoneyShort(p->wage).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The going rate for this skill is about %s a day. Underpaid directors drift away.", fmtMoney(fair).c_str());
            ImGui::EndTable();
        }
        if (p->trait != Trait::None) textDim("Trait: %s", traitName(p->trait));
    }
    // choose who sits here
    ImGui::SetNextItemWidth(260);
    std::string prev = p ? p->name : "Choose a director...";
    if (ImGui::BeginCombo("##dirpick", prev.c_str())) {
        if (p && ImGui::Selectable("Leave the seat empty")) {
            Command cm; cm.type = Cmd::AssignExec; cm.a = NONE; cm.b = seat;
            app.run(cm);
        }
        int n = 0;
        for (auto& q : w.people) {
            if (!q.alive || q.employer != w.player || q.role != Role::Executive) continue;
            n++;
            int held = -1;
            for (int s = 0; s < SEAT_COUNT; s++)
                if (c.exec[s] == q.id) held = s;
            std::string l = q.name + "  ·  skill " + std::to_string(q.mgmt) + (q.seat == seat ? "  ·  natural fit" : "") +
                            (held >= 0 ? std::string("  (now ") + seatName(held) + ")" : "");
            ImGui::PushID(q.id);
            if (ImGui::Selectable(l.c_str(), p && p->id == q.id)) {
                Command cm; cm.type = Cmd::AssignExec; cm.a = q.id; cm.b = seat;
                app.run(cm);
            }
            ImGui::PopID();
        }
        if (!n) ImGui::TextDisabled("No executives on staff");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Recruit...")) app.openWin(W_PEOPLE);
    ImGui::EndGroup();
}

// The limits a director works inside. Every change is sent at once.
static void policyEditor(App& app, int d) {
    World& w = app.world;
    DivPolicy pol = w.pc().divs[d].pol;
    bool ch = false;
    ch |= ImGui::Checkbox("Let the director run this division", &pol.automate);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: the division's fleets wait for your orders. On: the director gives them.");
    if (d != DIV_INTEL) {
        header("Limits");
        textDim("Base");
        ImGui::SameLine(150);
        Id base = pol.base == NONE ? w.pc().home : pol.base;
        if (stationPicker(app, "##base", base, 280)) { pol.base = base; ch = true; }
        if (d != DIV_INDUSTRY) {
            textDim("Works within");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(200);
            ch |= ImGui::SliderInt("jumps of the base", &pol.radius, 1, 12);
            textDim("Zones");
            ImGui::SameLine(150);
            bool zc = pol.zones & 1, zk = pol.zones & 2, zf = pol.zones & 4;
            ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(Zone::Charter));
            bool a = ImGui::Checkbox("Charter", &zc);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(Zone::Contract));
            bool b = ImGui::Checkbox("Contract", &zk);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(Zone::Frontier));
            bool cc = ImGui::Checkbox("Frontier", &zf);
            ImGui::PopStyleColor();
            if (a || b || cc) {
                pol.zones = (uint8_t)((zc ? 1 : 0) | (zk ? 2 : 0) | (zf ? 4 : 0));
                if (!pol.zones) pol.zones = 1;
                ch = true;
            }
            textDim("Riskiest system");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(200);
            char fmt[32];
            std::snprintf(fmt, sizeof fmt, "%%d  (%s)", riskGrade((float)pol.maxRisk));
            ch |= ImGui::SliderInt("##risk", &pol.maxRisk, 5, 100, fmt);
        }
    }
    if (const char* bl = budgetLabel(d)) {
        textDim("%s", bl);
        ImGui::SameLine(320);
        Money b = pol.budget;
        if (inputCredits("##budget", b, 160)) { pol.budget = b; ch = true; }
        if (d != DIV_TRADE) {
            ImGui::SameLine();
            textDim("spent this month %s", fmtMoneyShort(w.pc().divs[d].spent).c_str());
        }
    }
    header("Orders");
    switch (d) {
        case DIV_MINING:
            ch |= ImGui::Checkbox("Sell the minerals where the ore is refined", &pol.sell);
            if (!pol.sell) textDim("Minerals stay in the hangar: Haulage can bring them to the Industry base.");
            break;
        case DIV_TRADE: ch |= ImGui::Checkbox("Insure every cargo", &pol.insure); break;
        case DIV_HAULAGE:
            ch |= ImGui::Checkbox("Keep outposts fuelled", &pol.fuel);
            ch |= ImGui::Checkbox("Keep colonies supplied", &pol.supply);
            ch |= ImGui::Checkbox("Carry materials to your projects", &pol.projects);
            ch |= ImGui::Checkbox("Take courier contracts", &pol.couriers);
            ch |= ImGui::Checkbox("Bring stock home to the base", &pol.consolidate);
            ch |= ImGui::Checkbox("Insure every cargo", &pol.insure);
            break;
        case DIV_INDUSTRY: {
            ImGui::SetNextItemWidth(280);
            std::string prev = pol.product < 0 ? "The director picks the most profitable" : item(blueprint(pol.product).product).name;
            if (ImGui::BeginCombo("Build", prev.c_str())) {
                if (ImGui::Selectable("The director picks the most profitable", pol.product < 0)) { pol.product = -1; ch = true; }
                Id H = w.divisionBase(w.player, DIV_INDUSTRY);
                for (int bp = 0; bp < BP_OUTPOST; bp++) {
                    const BlueprintDef& bd = blueprint(bp);
                    bool own = w.have(w.player, H, bd.bpo) > 0 || (bd.bpc >= 0 && w.have(w.player, H, bd.bpc) > 0);
                    std::string l = item(bd.product).name + (own ? "" : "  (no blueprint at the base)");
                    ImGui::PushID(bp);
                    if (ImGui::Selectable(l.c_str(), pol.product == bp)) { pol.product = bp; ch = true; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(160);
            ch |= ImGui::SliderInt("lines at once", &pol.lines, 1, 4);
            if (const Person* p = w.director(w.player, d))
                if (1 + p->mgmt / 30 < pol.lines) {
                    ImGui::SameLine();
                    textColored(col::Warn, "this director can run %d", 1 + p->mgmt / 30);
                }
            ch |= ImGui::Checkbox("Sell what it builds", &pol.sell);
            ch |= ImGui::Checkbox("Research blueprints when a line is free", &pol.research);
            break;
        }
        case DIV_SECURITY:
            ch |= ImGui::Checkbox("Escort miners and haulers outside Charter space", &pol.escort);
            ch |= ImGui::Checkbox("Defend besieged outposts", &pol.guard);
            ch |= ImGui::Checkbox("Hunt bounties near the base", &pol.bounties);
            break;
        case DIV_INTEL: textDim("With the director running it, counter-intelligence works at full strength."); break;
    }
    if (ch) {
        Command cm;
        cm.type = Cmd::SetDivision;
        cm.a = d;
        cm.policy = pol;
        bool toggled = pol.automate != w.pc().divs[d].pol.automate;
        app.run(cm, toggled);
    }
}

static bool fleetFits(World& w, const Fleet& f, int d) {
    switch (d) {
        case DIV_MINING: return w.fleetOreCap(f) > 0;
        case DIV_TRADE:
        case DIV_HAULAGE: return w.fleetCargoCap(f) >= 1000;
        case DIV_SECURITY: return w.fleetStrength(f) > 0;
    }
    return false;
}

static void fleetsPanel(App& app, int d) {
    World& w = app.world;
    header("Fleets in the division");
    std::vector<Id> mine, others;
    for (Id fid : w.pc().fleetList) {
        const Fleet& f = w.fleets[fid];
        if (!f.alive) continue;
        if (f.division == d) mine.push_back(fid);
        else if (fleetFits(w, f, d)) others.push_back(fid);
    }
    if (mine.empty()) textDim("None yet. Add fleets below; a manual order to one of them hands it back to you.");
    else if (beginTable("dfleets", 5, 0, ImVec2(0, std::min(220.0f, 34.0f + 26.0f * mine.size())))) {
        ImGui::TableSetupColumn("Fleet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ships");
        ImGui::TableSetupColumn("Where");
        ImGui::TableSetupColumn("Doing", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (Id fid : mine) {
            const Fleet& f = w.fleets[fid];
            ImGui::TableNextRow();
            ImGui::PushID(fid);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(f.name.c_str(), app.selFleet == fid)) { app.selFleet = fid; app.openWin(W_FLEETS); }
            ImGui::TableNextColumn();
            cellInt((int64_t)f.ships.size());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(w.fleetLocation(f).c_str());
            ImGui::TableNextColumn();
            textDim("%s", f.status.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Take back")) {
                Command cm; cm.type = Cmd::AssignDivision; cm.a = fid; cm.b = -1;
                app.run(cm);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!others.empty()) {
        static Id pick = NONE;
        bool valid = std::find(others.begin(), others.end(), pick) != others.end();
        if (!valid) pick = others[0];
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##addfleet", w.fleets[pick].name.c_str())) {
            for (Id fid : others) {
                const Fleet& f = w.fleets[fid];
                std::string l = f.name + (f.division >= 0 ? std::string("  (") + divisionName(f.division) + ")" : "");
                ImGui::PushID(fid);
                if (ImGui::Selectable(l.c_str(), pick == fid)) pick = fid;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to the division")) {
            Command cm; cm.type = Cmd::AssignDivision; cm.a = pick; cm.b = d;
            app.run(cm);
        }
    } else if (mine.empty()) {
        textDim(d == DIV_MINING ? "Form a fleet of mining ships first (Fleets window)." :
                d == DIV_SECURITY ? "Form a fleet of warships first (Fleets window)." : "Form a fleet of haulers first (Fleets window).");
    }
}

static void reportsPanel(App& app, int d) {
    World& w = app.world;
    const DivisionState& ds = w.pc().divs[d];
    header("This month");
    if (ImGui::BeginTable("dmonth", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Revenue", fmtMoneyShort(ds.revenue), col::Good);
        ImGui::TableNextColumn(); kpi("Costs", fmtMoneyShort(ds.costs), col::Bad);
        Money r = ds.revenue - ds.costs;
        ImGui::TableNextColumn(); kpi("Result", fmtMoneyShort(r), r >= 0 ? col::Good : col::Bad);
        ImGui::EndTable();
    }
    textDim("%s", ds.status.empty() ? "-" : ds.status.c_str());
    if (ds.reports.empty()) return;
    header("Monthly reports");
    if (ds.reports.size() >= 2 && ImPlot::BeginPlot("##drep", ImVec2(-1, 120), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        std::vector<double> x, y;
        for (auto& r : ds.reports) { x.push_back(r.month); y.push_back(toCr(r.revenue - r.costs) / 1e6); }
        ImPlot::SetupAxes(nullptr, "result, M cr", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotBars("result", x.data(), y.data(), (int)x.size(), 0.6, {ImPlotProp_FillColor, col::v4(col::Accent)});
        ImPlot::EndPlot();
    }
    if (beginTable("dreports", 6, 0, ImVec2(0, std::min(200.0f, 34.0f + 26.0f * ds.reports.size())))) {
        ImGui::TableSetupColumn("Month");
        ImGui::TableSetupColumn("Revenue");
        ImGui::TableSetupColumn("Costs");
        ImGui::TableSetupColumn("Result");
        ImGui::TableSetupColumn("Fleets");
        ImGui::TableSetupColumn("Verdict", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = (int)ds.reports.size() - 1; i >= 0; i--) {
            const DivReport& r = ds.reports[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); textDim("%s", fmtDate((Tick)r.month * 30 * MINUTES_PER_DAY).substr(0, 7).c_str());
            ImGui::TableNextColumn(); cellMoney(r.revenue);
            ImGui::TableNextColumn(); cellMoney(r.costs);
            ImGui::TableNextColumn(); cellMoney(r.revenue - r.costs, true);
            ImGui::TableNextColumn(); cellInt(r.fleets);
            ImGui::TableNextColumn(); textColored(r.score > 0.1f ? col::Good : r.score < -0.2f ? col::Bad : col::Text, "%s", r.note.c_str());
        }
        ImGui::EndTable();
    }
}

static void staffRequests(App& app) {
    World& w = app.world;
    int n = 0;
    for (auto& r : w.staffReqs) {
        if (!r.open || r.expires <= w.now || w.people[r.person].employer != w.player) continue;
        if (!n++) header("Waiting for your answer");
        const Person& p = w.people[r.person];
        ImGui::PushID(r.id);
        std::string seat = "-";
        for (int s = 0; s < SEAT_COUNT; s++)
            if (w.pc().exec[s] == p.id) seat = seatName(s);
        switch (r.kind) {
            case StaffReqKind::Raise:
                textColored(col::Accent, "RAISE");
                ImGui::SameLine();
                ImGui::Text("%s (%s) asks for %s a day, up from %s.", p.name.c_str(), seat.c_str(), fmtMoney(r.wage).c_str(), fmtMoney(p.wage).c_str());
                break;
            case StaffReqKind::Poach:
                textColored(col::Warn, "POACH");
                ImGui::SameLine();
                ImGui::Text("%s offers %s (%s) %s a day.", w.corps[r.rival].name.c_str(), p.name.c_str(), seat.c_str(), fmtMoney(r.wage).c_str());
                break;
            case StaffReqKind::Notice:
                textColored(col::Bad, "NOTICE");
                ImGui::SameLine();
                ImGui::Text("%s (%s) is leaving unless paid %s a day.", p.name.c_str(), seat.c_str(), fmtMoney(r.wage).c_str());
                break;
        }
        ImGui::SameLine();
        textDim("(%s left)", fmtDuration(r.expires - w.now).c_str());
        if (accentButton(r.kind == StaffReqKind::Raise ? "Agree" : "Match it")) {
            Command cm; cm.type = Cmd::AnswerStaff; cm.a = r.id; cm.flag = true;
            app.run(cm);
        }
        ImGui::SameLine();
        if (ImGui::Button(r.kind == StaffReqKind::Raise ? "Decline" : "Let them go")) {
            Command cm; cm.type = Cmd::AnswerStaff; cm.a = r.id; cm.flag = false;
            app.run(cm);
        }
        ImGui::PopID();
    }
}

static void intelPanel(App& app) {
    World& w = app.world;
    header("New operation");
    static Id target = NONE;
    static int kind = 0, bp = 0;
    static char filt[48] = "";
    ImGui::SetNextItemWidth(280);
    std::string prev = target == NONE ? "Choose a rival..." : w.corps[target].name + "  [" + w.corps[target].ticker + "]";
    if (ImGui::BeginCombo("Target", prev.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##tf", "search", filt, sizeof filt);
        std::string f = upper(filt);
        int shown = 0;
        for (auto& k : w.corps) {
            if (k.player || !k.alive) continue;
            if (!f.empty() && upper(k.name + " " + k.ticker).find(f) == std::string::npos) continue;
            if (++shown > 200) break;
            ImGui::PushID(k.id);
            std::string l = k.name + "  [" + k.ticker + "]  " + archName(k.arch);
            if (ImGui::Selectable(l.c_str(), target == k.id)) target = k.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const char* kinds[] = {"Dossier (2 days): their fleets on your chart and their books for 10 days",
                           "Research theft (6 days): copy up to 2 levels of one blueprint's research",
                           "Sabotage (4 days): set back their jobs and an outpost's shield",
                           "Smear campaign (5 days): mark their shares down"};
    ImGui::SetNextItemWidth(520);
    ImGui::Combo("Operation", &kind, kinds, 4);
    if (kind == (int)SpyKind::Research) {
        ImGui::SetNextItemWidth(280);
        if (ImGui::BeginCombo("Blueprint", item(blueprint(bp).product).name.c_str())) {
            for (int b = 0; b < BLUEPRINT_COUNT; b++) {
                ImGui::PushID(b);
                char l[96];
                std::snprintf(l, sizeof l, "%s  (yours ME %d TE %d)", item(blueprint(b).product).name.c_str(), w.pc().me[b], w.pc().te[b]);
                if (ImGui::Selectable(l, bp == b)) bp = b;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    if (target != NONE) {
        Money cost = w.spyCost(target, (SpyKind)kind);
        double odds = w.spyChance(w.player, target, (SpyKind)kind);
        textDim("Costs %s  ·  odds about %d%%  ·  a failure can be traced back to you: standing and Charter standing fall.",
                fmtMoneyShort(cost).c_str(), (int)std::round(odds * 100));
        ImGui::BeginDisabled(!w.director(w.player, DIV_INTEL));
        if (accentButton("Launch the operation")) {
            Command cm; cm.type = Cmd::StartSpyOp; cm.a = kind; cm.b = target; cm.item = bp;
            app.run(cm);
        }
        ImGui::EndDisabled();
    }
    header("Operations");
    std::vector<int> rows;
    for (int i = (int)w.spyOps.size() - 1; i >= 0; i--)
        if (w.spyOps[i].corp == w.player) rows.push_back(i);
    if (rows.empty()) textDim("None yet.");
    else if (beginTable("ops", 4, 0, ImVec2(0, std::min(200.0f, 34.0f + 26.0f * rows.size())))) {
        ImGui::TableSetupColumn("Against");
        ImGui::TableSetupColumn("Operation");
        ImGui::TableSetupColumn("When");
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i : rows) {
            const SpyOp& op = w.spyOps[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(w.corps[op.target].name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(spyKindName(op.kind));
            ImGui::TableNextColumn(); textDim("%s", op.active ? ("reports in " + fmtDuration(op.end - w.now)).c_str() : fmtDate(op.end).c_str());
            ImGui::TableNextColumn();
            if (op.active) textDim("Under way");
            else textColored(op.outcome > 0 ? col::Good : op.outcome == -2 ? col::Bad : col::Warn, "%s", op.note.c_str());
        }
        ImGui::EndTable();
    }
    // open dossiers
    int nd = 0;
    for (auto& [cid, until] : w.dossiers) {
        if (until <= w.now) continue;
        if (!nd++) header("Open dossiers");
        const Corp& k = w.corps[cid];
        ImGui::PushID(cid);
        ImGui::PushFont(app.fonts.bold, kFontUi);
        ImGui::Text("%s  [%s]", k.name.c_str(), k.ticker.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        textDim("%s  ·  open for %s", archName(k.arch), fmtDuration(until - w.now).c_str());
        int fl = 0, sh = 0;
        for (Id fid : k.fleetList) fl += w.fleets[fid].alive;
        for (Id sid : k.shipList) sh += w.ships[sid].alive;
        int outposts = 0;
        for (Id o : w.outpostIds) outposts += !w.stations[o].dead && w.stations[o].owner == cid;
        textDim("Cash %s  ·  net worth %s  ·  owes %s  ·  %d fleets, %d ships, %d outposts  ·  Charter standing %+.1f", fmtMoneyShort(k.wallet).c_str(),
                fmtMoneyShort(k.netWorth).c_str(), fmtMoneyShort(w.loansOwed(cid)).c_str(), fl, sh, outposts, k.charterStanding);
        std::string res;
        for (int b = 0; b < BLUEPRINT_COUNT; b++)
            if (k.me[b] || k.te[b]) res += item(blueprint(b).product).name + " " + std::to_string(k.me[b]) + "/" + std::to_string(k.te[b]) + "  ";
        ImGui::PushTextWrapPos(0);
        textDim("Research (ME/TE): %s", res.empty() ? "none" : res.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
    // what rivals tried on you
    int caught = 0;
    for (auto& op : w.spyOps)
        if (op.target == w.player && !op.active && op.outcome == -2) caught++;
    header("Counter-intelligence");
    textDim("Defence against rival operations: about %d%% of what a top team manages. Caught so far: %d.",
            (int)std::round(std::min(1.0, 0.15 + (w.director(w.player, DIV_INTEL) ? w.director(w.player, DIV_INTEL)->mgmt / 100.0 * 0.75 : 0)) * 100), caught);
}

void App::winDivisions() {
    if (!beginWindow(*this, W_DIVS, ImVec2(180, 90), ImVec2(900, 680))) { ImGui::End(); return; }
    World& w = world;
    Corp& c = w.pc();
    staffRequests(*this);
    static int req = -1;
    if (divTabReq >= 0) { req = divTabReq; divTabReq = -1; }
    if (ImGui::BeginTabBar("divtabs")) {
        if (ImGui::BeginTabItem("Overview", nullptr, req == -2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::PushTextWrapPos(0);
            textDim("Each division is run by the executive in its seat. Give it fleets and limits, switch it on, and its director gives the "
                    "orders, crews the ships and reports every 30 days. A manual order to one of its fleets hands that fleet back to you.");
            ImGui::PopTextWrapPos();
            if (beginTable("divoverview", 7, 0, ImVec2(0, 34.0f + 27.0f * (float)(int)DIV_COUNT))) {
                auto fixed = [](const char* label, float min) {  // wide enough for the header and the values
                    ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed,
                                            std::max(min, ImGui::CalcTextSize(label).x + ImGui::GetStyle().CellPadding.x * 2 + 4));
                };
                fixed("Division", 90);
                ImGui::TableSetupColumn("Director", ImGuiTableColumnFlags_WidthStretch);
                fixed("Runs itself", 0);
                fixed("Fleets", 0);
                fixed("Month so far", 110);
                fixed("Last month", 110);
                ImGui::TableSetupColumn("Doing", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (int d = 0; d < DIV_COUNT; d++) {
                    const DivisionState& ds = c.divs[d];
                    const Person* p = w.director(w.player, d);
                    int fl = 0;
                    for (Id fid : c.fleetList) fl += w.fleets[fid].alive && w.fleets[fid].division == d;
                    ImGui::TableNextRow();
                    ImGui::PushID(d);
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(divisionName(d), false, ImGuiSelectableFlags_SpanAllColumns)) req = d;
                    ImGui::TableNextColumn();
                    if (p) ImGui::Text("%s  ·  %d", p->name.c_str(), p->mgmt);
                    else textColored(col::Warn, "vacant");
                    ImGui::TableNextColumn();
                    textColored(ds.pol.automate && p ? col::Good : col::Dim, "%s", ds.pol.automate ? (p ? "yes" : "waiting") : "no");
                    ImGui::TableNextColumn();
                    if (d == DIV_INDUSTRY || d == DIV_INTEL) textDim("-");
                    else cellInt(fl);
                    ImGui::TableNextColumn();
                    Money now = ds.revenue - ds.costs;
                    rightText(fmtMoneyShort(now, false), now > 0 ? col::Good : now < 0 ? col::Bad : col::Dim);
                    ImGui::TableNextColumn();
                    if (ds.reports.empty()) textDim("-");
                    else {
                        Money last = ds.reports.back().revenue - ds.reports.back().costs;
                        rightText(fmtMoneyShort(last, false), last > 0 ? col::Good : last < 0 ? col::Bad : col::Dim);
                    }
                    ImGui::TableNextColumn();
                    textDim("%s", ds.status.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        for (int d = 0; d < DIV_COUNT; d++) {
            ImGuiTabItemFlags fl = req == d ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(divisionName(d), nullptr, fl)) {
                ImGui::BeginChild("divbody");
                ImGui::PushTextWrapPos(0);
                textDim("%s", divBlurb(d));
                ImGui::PopTextWrapPos();
                header("Director");
                directorCard(*this, d);
                policyEditor(*this, d);
                if (d == DIV_INTEL) intelPanel(*this);
                else {
                    if (d != DIV_INDUSTRY) fleetsPanel(*this, d);
                    reportsPanel(*this, d);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    req = -1;
    ImGui::End();
}

}  // namespace nb
