// NebulaOne - the Contract Board and Territory (outposts and sovereignty).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static std::string contractWhat(const World& w, const Contract& c) {
    switch (c.kind) {
        case ContractKind::Supply: return "Deliver " + fmtInt(c.qty) + " " + item(c.item).name + " to " + w.stations[c.to].name;
        case ContractKind::Courier:
            return "Carry " + fmtInt(c.qty) + " " + item(c.item).name + " (" + fmtQty(c.qty * item(c.item).volume) + " m3) " +
                   w.stations[c.from].name + " > " + w.stations[c.to].name;
        case ContractKind::Bounty: return "Destroy " + fmtInt(c.goal) + " ships of " + w.corps[c.target].name;
        case ContractKind::Patrol: return "Patrol " + w.systems[c.system].name + " with warships for " + fmtInt(c.goal) + " h";
    }
    return "";
}

static Id contractSystem(const World& w, const Contract& c) {
    switch (c.kind) {
        case ContractKind::Supply: return w.stations[c.to].system;
        case ContractKind::Courier: return w.stations[c.from].system;
        default: return c.system;
    }
}

void App::winContracts() {
    if (!beginWindow(*this, W_CONTRACTS, ImVec2(140, 100), ImVec2(1000, 560))) { ImGui::End(); return; }
    World& w = world;
    Id homeSys = w.stations[w.pc().home].system;
    const auto& row = w.jumpsFrom(homeSys);
    if (ImGui::BeginTabBar("ctabs")) {
        if (ImGui::BeginTabItem("Board")) {
            static int kind = -1;
            static int maxJ = 12;
            const char* kinds[] = {"All work", "Supply", "Courier", "Bounty", "Patrol"};
            ImGui::SetNextItemWidth(140);
            int k1 = kind + 1;
            if (ImGui::Combo("##kind", &k1, kinds, 5)) kind = k1 - 1;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            ImGui::SliderInt("max jumps from home", &maxJ, 2, 40);
            ImGui::SameLine();
            textDim("Supply pays on delivery. Courier work holds collateral until you deliver.");
            std::vector<Id> ids;
            for (auto& c : w.contracts)
                if (c.state == ContractState::Open && c.expires > w.now && (kind < 0 || (int)c.kind == kind) && c.issuer != w.player &&
                    row[contractSystem(w, c)] <= maxJ)
                    ids.push_back(c.id);
            if (ids.empty()) textDim("Nothing on the board that matches. New work is posted every day.");
            else if (beginTable("board", 8, ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Issuer");
                ImGui::TableSetupColumn("Work", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Risk");
                ImGui::TableSetupColumn("Jumps", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("Reward", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Collateral");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                int sc = 4;
                bool asc = true;
                sortSpec(sc, asc);
                std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
                    const Contract &A = w.contracts[a], &B = w.contracts[b];
                    double va = 0, vb = 0;
                    switch (sc) {
                        case 0: va = (int)A.kind; vb = (int)B.kind; break;
                        case 5: va = (double)A.reward; vb = (double)B.reward; break;
                        case 6: va = (double)A.collateral; vb = (double)B.collateral; break;
                        case 3: va = w.systems[contractSystem(w, A)].risk; vb = w.systems[contractSystem(w, B)].risk; break;
                        default: va = row[contractSystem(w, A)]; vb = row[contractSystem(w, B)]; break;
                    }
                    return asc ? va < vb : va > vb;
                });
                Id accept = NONE;
                for (Id id : ids) {
                    const Contract& c = w.contracts[id];
                    Id sys = contractSystem(w, c);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); textColored(col::Accent, "%s", upper(contractKindName(c.kind)).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(c.issuerName.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(contractWhat(w, c).c_str());
                    ImGui::TableNextColumn(); riskBadge(w.systems[sys].risk);
                    ImGui::TableNextColumn(); cellInt(row[sys]);
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(c.reward, false), col::Good);
                    ImGui::TableNextColumn(); if (c.collateral) cellMoney(c.collateral); else rightText("-", col::Dim);
                    ImGui::TableNextColumn();
                    ImGui::PushID(id);
                    if (ImGui::SmallButton("Accept")) accept = id;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Map")) { selectSystem(sys, false); centerMap(sys); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (accept != NONE) { Command cm; cm.type = Cmd::AcceptContract; cm.a = accept; run(cm); }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("My work")) {
            int n = 0;
            if (beginTable("mine", 6)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Work", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Progress");
                ImGui::TableSetupColumn("Deadline");
                ImGui::TableSetupColumn("Reward");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (int i = (int)w.contracts.size() - 1; i >= 0; i--) {
                    const Contract& c = w.contracts[i];
                    if (c.contractor != w.player) continue;
                    if (c.state != ContractState::Accepted && w.now - c.deadline > 5 * MINUTES_PER_DAY) continue;
                    n++;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); textColored(col::Accent, "%s", upper(contractKindName(c.kind)).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(contractWhat(w, c).c_str());
                    ImGui::TableNextColumn();
                    if (c.state != ContractState::Accepted) {
                        textColored(c.state == ContractState::Done ? col::Good : col::Bad, "%s", contractStateName(c.state));
                    } else if (c.kind == ContractKind::Supply || c.kind == ContractKind::Courier) {
                        int64_t have = w.have(w.player, c.to, c.item);
                        progressBar((float)have / (float)std::max<int64_t>(1, c.qty), ImVec2(90, 0));
                        ImGui::SameLine();
                        textDim("%s at drop-off", fmtInt(have).c_str());
                    } else {
                        progressBar((float)c.progress / (float)std::max<int64_t>(1, c.goal), ImVec2(90, 0));
                        ImGui::SameLine();
                        textDim("%s / %s", fmtInt(c.progress).c_str(), fmtInt(c.goal).c_str());
                    }
                    ImGui::TableNextColumn();
                    if (c.state == ContractState::Accepted) monoText(fmtDuration(c.deadline - w.now), c.deadline - w.now < MINUTES_PER_DAY ? col::Bad : col::Text);
                    ImGui::TableNextColumn(); cellMoney(c.reward);
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (c.state == ContractState::Accepted && (c.kind == ContractKind::Supply || c.kind == ContractKind::Courier)) {
                        if (ImGui::SmallButton("Deliver")) { Command cm; cm.type = Cmd::DeliverContract; cm.a = c.id; run(cm); }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Haul it")) {
                            // prefill a Haul order for the selected (or first) fleet
                            openWin(W_FLEETS);
                            if (selFleet != NONE) {
                                draft.fleet = selFleet;
                                draft.o = FleetOrder{};
                                draft.o.kind = OrderKind::Haul;
                                draft.o.station = c.kind == ContractKind::Courier ? c.from : w.pc().home;
                                draft.o.station2 = c.to;
                                draft.o.item = c.item;
                                draft.o.qty = c.qty;
                                draft.o.contract = c.id;
                                draft.o.policy = RoutePolicy::LowestRisk;
                                toast("Haul order drafted in Fleets: check it and press Issue order. It delivers on arrival.");
                            } else {
                                toast("Select a fleet in the Fleets window first.", false);
                            }
                        }
                    }
                    if (c.kind == ContractKind::Patrol && c.state == ContractState::Accepted && ImGui::SmallButton("Patrol order")) {
                        openWin(W_FLEETS);
                        if (selFleet != NONE) {
                            draft.fleet = selFleet;
                            draft.o = FleetOrder{};
                            draft.o.kind = OrderKind::Patrol;
                            draft.o.system = c.system;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (!n) textDim("You have no contracts in progress.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Post work")) {
            static int kind = 1;
            static Contract draftC;
            static Money reward = 0;
            static int64_t qty = 1000, goal = 3;
            ImGui::RadioButton("Courier my goods", &kind, 1); ImGui::SameLine();
            ImGui::RadioButton("Buy delivered", &kind, 0); ImGui::SameLine();
            ImGui::RadioButton("Bounty", &kind, 2); ImGui::SameLine();
            ImGui::RadioButton("Patrol", &kind, 3);
            textDim("The reward is held in escrow and returned if nobody takes the job within 3 days. AI traders and mercenaries take work too.");
            if (draftC.from == NONE) draftC.from = w.pc().home;
            if (draftC.item < 0) draftC.item = RATIONS;
            if (kind == 1) {
                stationPicker(*this, "Pick up at", draftC.from);
                stationPicker(*this, "Deliver to", draftC.to);
                itemPicker("Goods", draftC.item, 220, false, true);
                inputQty("Quantity", qty);
                ImGui::SameLine();
                textDim("you hold %s there", fmtInt(draftC.from != NONE && draftC.item >= 0 ? w.have(w.player, draftC.from, draftC.item) : 0).c_str());
            } else if (kind == 0) {
                stationPicker(*this, "Deliver to", draftC.to);
                itemPicker("Goods", draftC.item, 220, false, true);
                inputQty("Quantity", qty);
            } else if (kind == 2) {
                corpPicker(*this, "Target", draftC.target, 260, false);
                inputQty("Ships to destroy", goal);
            } else {
                systemPicker(*this, "System", draftC.system);
                inputQty("Hours on station", goal);
            }
            inputCredits("Reward", reward, 160);
            if (accentButton("Post on the board", ImVec2(180, 0))) {
                Command cm;
                cm.type = Cmd::PostContract;
                cm.contract = draftC;
                cm.contract.kind = (ContractKind)kind;
                cm.contract.qty = qty;
                cm.contract.goal = kind >= 2 ? goal : 5;
                cm.contract.reward = reward;
                run(cm);
            }
            header("Your postings");
            for (int i = (int)w.contracts.size() - 1; i >= 0; i--) {
                const Contract& c = w.contracts[i];
                if (c.issuer != w.player) continue;
                if (c.state != ContractState::Open && c.state != ContractState::Accepted && w.now - c.posted > 10 * MINUTES_PER_DAY) continue;
                ImGui::PushID(i);
                textColored(col::Accent, "%s", upper(contractKindName(c.kind)).c_str());
                ImGui::SameLine(90);
                ImGui::TextUnformatted(contractWhat(w, c).c_str());
                ImGui::SameLine();
                textDim("%s  %s", contractStateName(c.state), c.contractor != NONE ? w.corps[c.contractor].ticker.c_str() : "");
                if (c.state == ContractState::Open) { ImGui::SameLine(); if (ImGui::SmallButton("Withdraw")) { Command cm; cm.type = Cmd::CancelContract; cm.a = c.id; run(cm); } }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- territory --
void App::winTerritory() {
    if (!beginWindow(*this, W_TERRITORY, ImVec2(180, 110), ImVec2(940, 560))) { ImGui::End(); return; }
    World& w = world;
    Id homeSys = w.stations[w.pc().home].system;
    if (ImGui::BeginTabBar("ttabs")) {
        if (ImGui::BeginTabItem("Your outposts")) {
            std::vector<Id> mine;
            for (Id o : w.outpostIds)
                if (!w.stations[o].dead && w.stations[o].owner == w.player) mine.push_back(o);
            if (mine.empty()) {
                textDim("You hold no territory yet.");
                header("How to claim a Frontier system");
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(
                    "1. Get an Outpost Kit: buy one at a Contract-space trade hub, or build it from the Outpost Kit Blueprint.\n"
                    "2. It is 40,000 m3: only a Carrack's hold takes it. Haul it out with escorts.\n"
                    "3. Fly into a Frontier system nobody holds and press Anchor outpost in the Fleets window.\n"
                    "4. Anchoring takes 24 hours. Then keep Fuel Cells in its hangar: 250 a day, or it goes offline.\n\n"
                    "An online outpost gives you the system: a market and refinery with 70% yield, and taxes from everyone who "
                    "trades or refines there. Others can besiege it: shield first, then a 24-hour reinforcement, then the hull.");
                ImGui::PopTextWrapPos();
            } else if (beginTable("outposts", 9)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Outpost", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State");
                ImGui::TableSetupColumn("Shield");
                ImGui::TableSetupColumn("Hull");
                ImGui::TableSetupColumn("Fuel");
                ImGui::TableSetupColumn("Market tax");
                ImGui::TableSetupColumn("Refinery tax");
                ImGui::TableSetupColumn("Collected");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (Id o : mine) {
                    const Station& st = w.stations[o];
                    ImGui::TableNextRow();
                    ImGui::PushID(o);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(st.name.c_str());
                    if (st.besieger != NONE && w.now - st.lastSiege < 120) { ImGui::SameLine(); textColored(col::Bad, "UNDER SIEGE by %s", w.corps[st.besieger].ticker.c_str()); }
                    ImGui::TableNextColumn();
                    ImU32 sc = st.state == OutpostState::Online ? col::Good : st.state == OutpostState::Anchoring ? col::Accent : col::Bad;
                    textColored(sc, "%s", outpostStateName(st.state));
                    if (st.state == OutpostState::Anchoring || st.state == OutpostState::Reinforced) { ImGui::SameLine(); textDim("%s", fmtDuration(st.stateUntil - w.now).c_str()); }
                    ImGui::TableNextColumn(); progressBar((float)st.shield, ImVec2(60, 0), col::Info);
                    ImGui::TableNextColumn(); progressBar((float)st.hull, ImVec2(60, 0), st.hull < 0.5 ? col::Bad : col::Good);
                    ImGui::TableNextColumn();
                    int64_t fuel = w.have(w.player, o, FUEL_CELLS);
                    rightText(fmtInt(fuel / OUTPOST_FUEL_PER_DAY) + " d", fuel < OUTPOST_FUEL_PER_DAY * 3 ? col::Bad : col::Text);
                    float mt = st.marketTax * 100, rt = st.refineTax * 100;
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(80);
                    bool ch = ImGui::SliderFloat("##mt", &mt, 0, 25, "%.0f%%");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(80);
                    ch |= ImGui::SliderFloat("##rt", &rt, 0, 25, "%.0f%%");
                    if (ch) { Command cm; cm.type = Cmd::SetOutpostTax; cm.a = o; cm.f = mt / 100; cm.f2 = rt / 100; run(cm, false); }
                    ImGui::TableNextColumn(); cellMoney(st.taxCollected);
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Market")) showMarket(o, FUEL_CELLS, 0);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Map")) { selectSystem(st.system, false); centerMap(st.system); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            header("Outpost kits you own");
            bool any = false;
            for (auto& [k, q] : w.pc().hangar)
                if ((int)(k % HANGAR_KEY) == OUTPOST_KIT) { any = true; ImGui::Text("%s x %s", w.stations[(Id)(k / HANGAR_KEY)].name.c_str(), fmtInt(q).c_str()); }
            for (Id fid : w.pc().fleetList) {
                const Fleet& f = w.fleets[fid];
                if (f.alive && f.cargo.count(OUTPOST_KIT)) { any = true; textColored(col::Accent, "Aboard %s at %s", f.name.c_str(), w.fleetLocation(f).c_str()); }
            }
            if (!any) {
                textDim("None.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Find a kit on the market")) showMarket(NONE, OUTPOST_KIT, 2);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sovereignty")) {
            textDim("Every outpost on the chart. Holders defend them with patrols; a Siege order starts the clock.");
            if (beginTable("sov", 7)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Holder");
                ImGui::TableSetupColumn("State");
                ImGui::TableSetupColumn("Shield / hull");
                ImGui::TableSetupColumn("Jumps");
                ImGui::TableSetupColumn("Risk");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                const auto& row = w.jumpsFrom(homeSys);
                std::vector<Id> ids;
                for (Id o : w.outpostIds)
                    if (!w.stations[o].dead) ids.push_back(o);
                std::sort(ids.begin(), ids.end(), [&](Id a, Id b) { return row[w.stations[a].system] < row[w.stations[b].system]; });
                for (Id o : ids) {
                    const Station& st = w.stations[o];
                    const StarSystem& sy = w.systems[st.system];
                    ImGui::TableNextRow();
                    ImGui::PushID(o);
                    ImGui::TableNextColumn(); ImGui::Text("%s  %s", sy.name.c_str(), sy.hexId.c_str());
                    ImGui::TableNextColumn(); textColored(st.owner == w.player ? col::Accent : col::Text, "%s", w.corps[st.owner].name.c_str());
                    ImGui::TableNextColumn(); textDim("%s", outpostStateName(st.state));
                    ImGui::TableNextColumn(); textDim("%.0f%% / %.0f%%", st.shield * 100, st.hull * 100);
                    ImGui::TableNextColumn(); cellInt(row[st.system]);
                    ImGui::TableNextColumn(); riskBadge(sy.risk, sy.riskLive);
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Map")) { selectSystem(st.system, false); centerMap(st.system); }
                    if (st.owner != w.player) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Besiege")) {
                            openWin(W_FLEETS);
                            if (selFleet != NONE) {
                                draft.fleet = selFleet;
                                draft.o = FleetOrder{};
                                draft.o.kind = OrderKind::Siege;
                                draft.o.station = o;
                                draft.o.system = st.system;
                            }
                        }
                    }
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
