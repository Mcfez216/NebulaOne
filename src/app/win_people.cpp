// NebulaOne - People: staff, recruitment and the executive board.
#include <algorithm>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static void skillCell(int v) {
    ImU32 c = v >= 75 ? col::Good : v < 30 ? col::Bad : col::Text;
    rightText(std::to_string(v), c);
}

void App::winPeople() {
    if (!beginWindow(*this, W_PEOPLE, ImVec2(160, 120), ImVec2(860, 520))) { ImGui::End(); return; }
    World& w = world;
    if (ImGui::BeginTabBar("ptabs")) {
        if (ImGui::BeginTabItem("Staff")) {
            Id fire = NONE;
            if (beginTable("staff", 9)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Role");
                ImGui::TableSetupColumn("Mining");
                ImGui::TableSetupColumn("Navigation");
                ImGui::TableSetupColumn("Gunnery");
                ImGui::TableSetupColumn("Management");
                ImGui::TableSetupColumn("Trait");
                ImGui::TableSetupColumn("Wage / day");
                ImGui::TableSetupColumn("Assignment", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                Money total = 0;
                for (auto& p : w.people) {
                    if (!p.alive || p.employer != w.player) continue;
                    total += p.wage;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(p.id);
                    ImGui::TextUnformatted(p.name.c_str());
                    if (ImGui::BeginPopupContextItem("ctx")) {
                        if (ImGui::MenuItem(("Dismiss " + p.name).c_str())) fire = p.id;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(p.role == Role::Pilot ? "Pilot" : "Executive");
                    ImGui::TableNextColumn(); skillCell(p.mining);
                    ImGui::TableNextColumn(); skillCell(p.nav);
                    ImGui::TableNextColumn(); skillCell(p.gunnery);
                    ImGui::TableNextColumn(); skillCell(p.mgmt);
                    ImGui::TableNextColumn(); textDim("%s", traitName(p.trait));
                    ImGui::TableNextColumn(); cellMoney(p.wage);
                    ImGui::TableNextColumn();
                    if (p.injuredUntil > w.now) textColored(col::Bad, "Injured, %s", fmtDuration(p.injuredUntil - w.now).c_str());
                    else if (p.ship != NONE) ImGui::Text("Flying %s #%d", shipDef(w.ships[p.ship].def).name.c_str(), p.ship);
                    else {
                        int seat = -1;
                        for (int s = 0; s < SEAT_COUNT; s++)
                            if (w.pc().exec[s] == p.id) seat = s;
                        if (seat >= 0) textColored(col::Accent, "%s", seatName(seat));
                        else textDim(p.role == Role::Pilot ? "Unassigned" : "No seat (specialist: %s)", seatName(p.seat));
                    }
                }
                ImGui::EndTable();
                textDim("Daily wages %s. Right-click a name to dismiss them.", fmtMoney(total).c_str());
            }
            if (fire != NONE) { Command c; c.type = Cmd::Fire; c.a = fire; run(c); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Recruit")) {
            textDim("Candidates wait at the Trade Hubs. New faces arrive every 7 days. Hiring costs 3 days' wages.");
            static int roleFilter = 0;
            ImGui::RadioButton("All", &roleFilter, 0); ImGui::SameLine();
            ImGui::RadioButton("Pilots", &roleFilter, 1); ImGui::SameLine();
            ImGui::RadioButton("Executives", &roleFilter, 2);
            if (beginTable("cands", 10, ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Role");
                ImGui::TableSetupColumn("Hub");
                ImGui::TableSetupColumn("Mining");
                ImGui::TableSetupColumn("Navigation");
                ImGui::TableSetupColumn("Gunnery");
                ImGui::TableSetupColumn("Management");
                ImGui::TableSetupColumn("Trait");
                ImGui::TableSetupColumn("Wage / day", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                std::vector<Id> ids;
                for (auto& p : w.people)
                    if (p.alive && p.employer == NONE && p.hub != NONE &&
                        (roleFilter == 0 || (roleFilter == 1) == (p.role == Role::Pilot)))
                        ids.push_back(p.id);
                int sc = 8;
                bool asc = true;
                sortSpec(sc, asc);
                std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
                    const Person &A = w.people[a], &B = w.people[b];
                    double va = 0, vb = 0;
                    switch (sc) {
                        case 3: va = A.mining; vb = B.mining; break;
                        case 4: va = A.nav; vb = B.nav; break;
                        case 5: va = A.gunnery; vb = B.gunnery; break;
                        case 6: va = A.mgmt; vb = B.mgmt; break;
                        case 8: va = (double)A.wage; vb = (double)B.wage; break;
                        default: return asc ? A.name < B.name : A.name > B.name;
                    }
                    return asc ? va < vb : va > vb;
                });
                for (Id id : ids) {
                    const Person& p = w.people[id];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name.c_str());
                    ImGui::TableNextColumn();
                    if (p.role == Role::Pilot) ImGui::TextUnformatted("Pilot");
                    else textColored(col::Accent, "Exec: %s", seatName(p.seat));
                    ImGui::TableNextColumn(); textDim("%s", w.systems[w.stations[p.hub].system].name.c_str());
                    ImGui::TableNextColumn(); skillCell(p.mining);
                    ImGui::TableNextColumn(); skillCell(p.nav);
                    ImGui::TableNextColumn(); skillCell(p.gunnery);
                    ImGui::TableNextColumn(); skillCell(p.mgmt);
                    ImGui::TableNextColumn(); textDim("%s", traitName(p.trait));
                    ImGui::TableNextColumn(); cellMoney(p.wage);
                    ImGui::TableNextColumn();
                    ImGui::PushID(id);
                    if (ImGui::SmallButton("Hire")) { Command c; c.type = Cmd::Hire; c.a = id; run(c); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace nb
