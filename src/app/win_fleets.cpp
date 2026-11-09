// NebulaOne - Fleets (orders, cargo, crews) and Assets (hangars and hulls).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static std::string shipName(const World& w, Id s) {
    const Ship& sh = w.ships[s];
    return sh.name.empty() ? shipDef(sh.def).name + " #" + std::to_string(s) : sh.name;
}

static void pilotCombo(App& a, Id ship) {
    World& w = a.world;
    Ship& s = w.ships[ship];
    std::string cur = s.pilot == NONE ? "(no pilot)" : w.people[s.pilot].name;
    ImGui::SetNextItemWidth(170);
    ImGui::PushID(ship);
    if (ImGui::BeginCombo("##pilot", cur.c_str())) {
        if (ImGui::Selectable("(no pilot)", s.pilot == NONE)) { Command c; c.type = Cmd::AssignPilot; c.a = NONE; c.b = ship; a.run(c, false); }
        for (auto& p : w.people) {
            if (!p.alive || p.employer != w.player || p.role != Role::Pilot) continue;
            char lab[128];
            std::snprintf(lab, sizeof lab, "%s  M%d N%d G%d%s%s", p.name.c_str(), p.mining, p.nav, p.gunnery,
                          p.ship != NONE && p.ship != ship ? "  (flying)" : "", p.injuredUntil > w.now ? "  (injured)" : "");
            ImGui::PushID(p.id);
            if (ImGui::Selectable(lab, s.pilot == p.id)) { Command c; c.type = Cmd::AssignPilot; c.a = p.id; c.b = ship; a.run(c, false); }
            ImGui::PopID();
        }
        if (ImGui::Selectable("Hire pilots...")) a.openWin(W_PEOPLE);
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

// ------------------------------------------------------------- order editor --
void App::fleetOrderEditor(Fleet& f) {
    World& w = world;
    OrderDraft& d = draft;
    Id here = f.docked != NONE ? f.docked : w.nearestStation(f.system);
    if (d.fleet != f.id) {
        d.fleet = f.id;
        d.o = f.order;
        if (d.o.kind == OrderKind::None) d.o.kind = OrderKind::Move;
        if (d.o.station == NONE) d.o.station = here;
        if (d.o.system == NONE) d.o.system = f.system;
        if (d.o.station2 == NONE) d.o.station2 = w.pc().home != here ? w.pc().home : NONE;
    }
    FleetOrder& o = d.o;
    static const OrderKind kinds[] = {OrderKind::None,   OrderKind::Move,  OrderKind::Mine,   OrderKind::Haul,    OrderKind::Trade, OrderKind::Patrol,
                                      OrderKind::Raid,   OrderKind::Siege, OrderKind::Survey, OrderKind::Salvage, OrderKind::Scout};
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("Order", orderKindName(o.kind))) {
        for (auto k : kinds)
            if (ImGui::Selectable(orderKindName(k), o.kind == k)) {
                o.kind = k;
                if (k == OrderKind::Trade && o.item < 0) o.item = RATIONS;
                if (k == OrderKind::Trade && (o.item < 0 || isShipItem(o.item) || isBpo(o.item))) o.item = RATIONS;
            }
        ImGui::EndCombo();
    }
    static const char* tips[] = {"Stay docked and wait.", "Fly to a station and dock.",
                                 "Mine a belt until full, unload at a station, repeat.", "Carry cargo from one station to another.",
                                 "Buy an item at one station and sell it at another.", "Hold a system and engage hostiles there.",
                                 "Lurk in a system and attack passing convoys. Piracy has consequences.",
                                 "Wear down an outpost's shield, then its hull. The owner gets 24 hours to respond.",
                                 "Scan a system for derelicts, relic caches, hidden ore and drift lanes.",
                                 "Strip a derelict or relic cache you have found and bring the haul home.",
                                 "Hold a system as a picket: your sensors there reach a jump further."};
    ImGui::SameLine();
    textDim("%s", tips[(int)o.kind]);
    Id dest = NONE;  // system the route preview goes to
    switch (o.kind) {
        case OrderKind::None: break;
        case OrderKind::Move:
            stationPicker(*this, "Destination", o.station);
            if (o.station != NONE) dest = w.stations[o.station].system;
            break;
        case OrderKind::Mine: {
            if (systemPicker(*this, "System", o.system)) { o.belt = -1; o.station = w.nearestStation(o.system); }
            if (o.system != NONE) {
                const StarSystem& s = w.systems[o.system];
                dest = o.system;
                for (int b = 0; b < (int)s.belts.size(); b++) {
                    const Belt& bl = s.belts[b];
                    if (!w.beltVisible(bl, w.player) || bl.capacity <= 0) continue;
                    ImGui::PushID(b);
                    char lab[96];
                    std::snprintf(lab, sizeof lab, "Belt %d: %s", b + 1, item(bl.ore).name.c_str());
                    if (ImGui::RadioButton(lab, o.belt == b)) o.belt = b;
                    ImGui::SameLine(200);
                    progressBar((float)(bl.remaining / std::max(1.0, bl.capacity)), ImVec2(120, 0), col::Accent);
                    if (bl.knownBy == w.player) { ImGui::SameLine(); textColored(col::Accent, "hidden"); }
                    ImGui::SameLine();
                    textDim("%s left  ·  ~%s per unit refined", fmtQty(bl.remaining).c_str(),
                            fmtMoney(item(bl.ore).base * 2 * 55 / 100, false).c_str());
                    if (item(bl.ore).depth > 0) { ImGui::SameLine(); textColored(zoneColor(s.zone), " depth %d ore", item(bl.ore).depth); }
                    ImGui::PopID();
                }
                if (o.belt < 0 && !s.belts.empty()) o.belt = 0;
                if (o.station == NONE) o.station = w.nearestStation(o.system);
                stationPicker(*this, "Unload at", o.station);
            }
            break;
        }
        case OrderKind::Haul:
            stationPicker(*this, "From", o.station);
            stationPicker(*this, "To", o.station2);
            itemPicker("Cargo", o.item, 200, true, true);
            ImGui::SameLine();
            inputQty("Max units per trip (0 = all that fits)", o.qty);
            ImGui::Checkbox("Repeat until cancelled", &o.loop);
            if (o.station != NONE) dest = w.stations[o.station].system;
            break;
        case OrderKind::Trade: {
            stationPicker(*this, "Buy at", o.station);
            stationPicker(*this, "Sell at", o.station2);
            if (itemPicker("Item", o.item, 200, false, true)) o.price = o.price2 = 0;
            if (o.station != NONE && o.station2 != NONE && o.item >= 0) {
                // what we know of the two books: live where our sensors reach, else the last snapshot
                auto known = [&](Id st, std::vector<Order>& sells, std::vector<Order>& buys) {
                    sells.clear();
                    buys.clear();
                    if (w.seen(w.stations[st].system)) { w.bookView(st, o.item, sells, buys); return w.now; }
                    auto itS = w.intel.find(st);
                    if (itS == w.intel.end()) return (Tick)-1;
                    if (const SnapRow* r = itS->second.find(o.item)) {
                        if (r->ask) { Order x; x.price = r->ask; x.qty = r->askQty; sells.push_back(x); }
                        if (r->bid) { Order x; x.price = r->bid; x.qty = r->bidQty; buys.push_back(x); }
                    }
                    return itS->second.t;
                };
                std::vector<Order> A, B, unused;
                Tick tA = known(o.station, A, unused), tB = known(o.station2, unused, B);
                Money ask = 0, bid = 0;
                for (auto& x : A) if (x.corp != w.player) { ask = x.price; break; }
                for (auto& x : B) if (x.corp != w.player) { bid = x.price; break; }
                if (o.price == 0) o.price = ask ? ask * 103 / 100 : item(o.item).base;
                if (o.price2 == 0) o.price2 = bid ? bid * 95 / 100 : item(o.item).base;
                inputCredits("Max buy price", o.price);
                ImGui::SameLine();
                textDim("best ask %s", ask ? fmtMoney(ask, false).c_str() : "-");
                inputCredits("Min sell price", o.price2);
                ImGui::SameLine();
                textDim("best bid %s", bid ? fmtMoney(bid, false).c_str() : "-");
                int64_t fit = (int64_t)(w.fleetCargoCap(f) / std::max(1e-9, item(o.item).volume));
                int64_t afford = o.price > 0 ? std::max<int64_t>(0, w.pc().wallet * 6 / 10 / o.price) : 0;
                if (o.qty <= 0) o.qty = std::min(fit, afford);
                inputQty("Units per trip", o.qty);
                ImGui::SameLine();
                textDim("holds %s, 60%% of wallet buys %s", fmtInt(fit).c_str(), fmtInt(afford).c_str());
                ImGui::Checkbox("Repeat until cancelled", &o.loop);
                int64_t q = 0;
                Money cost = 0;
                for (auto& x : A) {
                    if (x.corp == w.player || x.price > o.price || q >= o.qty) continue;
                    int64_t n = std::min(x.qty, o.qty - q);
                    q += n;
                    cost += x.price * n;
                }
                int64_t s = 0;
                Money rev = 0;
                for (auto& x : B) {
                    if (x.corp == w.player || x.price < o.price2 || s >= q) continue;
                    int64_t n = std::min(x.qty, q - s);
                    s += n;
                    rev += x.price * n;
                }
                Money profit = (Money)(rev * (1.0 - w.salesTax(w.player))) - cost;
                int jumps = (int)w.route(f.system, w.stations[o.station].system, o.policy).size() +
                            (int)w.route(w.stations[o.station].system, w.stations[o.station2].system, o.policy).size();
                header(tA == w.now && tB == w.now ? "Estimate at today's prices" : "Estimate from what you know");
                if (tA < 0 || tB < 0) textColored(col::Warn, "No intel on %s: the estimate is blind.", tA < 0 ? w.stations[o.station].name.c_str() : w.stations[o.station2].name.c_str());
                else if (tA != w.now || tB != w.now)
                    textColored(col::Warn, "Prices at %s are %s old.", tA != w.now ? w.stations[o.station].name.c_str() : w.stations[o.station2].name.c_str(),
                                fmtDuration(w.now - std::min(tA, tB)).c_str());
                ImGui::Text("Buy %s for %s, sell %s for %s:", fmtInt(q).c_str(), fmtMoneyShort(cost).c_str(), fmtInt(s).c_str(),
                            fmtMoneyShort(rev).c_str());
                ImGui::SameLine();
                textColored(profit > 0 ? col::Good : col::Bad, "%s per trip", fmtMoneyShort(profit).c_str());
                int hexes = 0;
                Id at = f.system;
                for (Id nx : w.route(f.system, w.stations[o.station].system, o.policy)) { hexes += w.laneHexes(at, nx); at = nx; }
                for (Id nx : w.route(w.stations[o.station].system, w.stations[o.station2].system, o.policy)) { hexes += w.laneHexes(at, nx); at = nx; }
                textDim("%d jumps (%d hexes), about %s. Prices move while you travel; unsold units stay in your hangar there.", jumps,
                        hexes, fmtDuration((Tick)(hexes * w.fleetMinutesPerHex(f)) + jumps + 30).c_str());
                if (q < s) {}
            }
            if (o.station != NONE) dest = w.stations[o.station2 != NONE ? o.station2 : o.station].system;
            break;
        }
        case OrderKind::Patrol:
        case OrderKind::Raid:
            systemPicker(*this, "System##pr", o.system);
            if (o.kind == OrderKind::Raid) {
                corpPicker(*this, "Target", o.targetCorp);
                if (o.system != NONE && w.systems[o.system].zone == Zone::Charter)
                    textColored(col::Bad, "Charter space: the Wardens arrive in round %d and destroy aggressors. Your Charter standing falls.",
                                w.wardenRound(o.system));
                else if (o.system != NONE && w.systems[o.system].zone == Zone::Contract)
                    textColored(col::Warn, "Contract space: no police, but the Charter still records it (a smaller standing hit).");
                else textDim("The Frontier: no law at all. Victims remember, and mercenaries hunt outlaws.");
            } else {
                textDim("Patrols engage outlaws and enemies, and defend your fleets in the system.");
            }
            dest = o.system;
            break;
        case OrderKind::Survey: {
            systemPicker(*this, "System##sv", o.system);
            ImGui::Checkbox("Keep going: then survey the nearest unsurveyed systems within 3 jumps", &o.loop);
            double scan = 0;
            for (Id s : f.ships) scan = std::max(scan, w.stats(w.ships[s]).scan);
            textDim("Survey strength %.2f: about %s a system. Wisps and Survey Scanners work faster and find more.", scan,
                    fmtDuration((Tick)(480.0 / (0.4 + scan))).c_str());
            dest = o.system;
            break;
        }
        case OrderKind::Salvage: {
            ImGui::SetNextItemWidth(320);
            const Site* cur = nullptr;
            for (auto& s : w.sites)
                if (s.id == o.site) cur = &s;
            if (ImGui::BeginCombo("Site", cur ? (cur->name + " in " + w.systems[cur->system].name).c_str() : "(choose a site you found)")) {
                for (auto& s : w.sites) {
                    if (s.finder != w.player || s.done || s.expires <= w.now || (s.kind != SiteKind::Derelict && s.kind != SiteKind::Relic)) continue;
                    ImGui::PushID(s.id);
                    if (ImGui::Selectable((s.name + " in " + w.systems[s.system].name).c_str(), o.site == s.id)) o.site = s.id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            stationPicker(*this, "Bring it to", o.station);
            if (cur) dest = cur->system;
            textDim("Bring hold space: a derelict yields components, modules and sometimes a blueprint copy; a relic cache rare minerals "
                    "and, sometimes, old research data.");
            break;
        }
        case OrderKind::Scout:
            systemPicker(*this, "System##sc", o.system);
            textDim("The fleet sits in space as a picket. Combine with a Wisp for a three-jump view.");
            dest = o.system;
            break;
        case OrderKind::Siege: {
            stationPicker(*this, "Outpost", o.station, 300, [&](const Station& st) { return st.isOutpost() && st.owner != w.player; });
            if (o.station != NONE && w.stations[o.station].isOutpost()) {
                const Station& op = w.stations[o.station];
                o.system = op.system;
                dest = op.system;
                textDim("Held by %s  ·  %s  ·  shield %.0f%%  hull %.0f%%", w.corps[op.owner].name.c_str(), outpostStateName(op.state),
                        op.shield * 100, op.hull * 100);
                textDim("Your fleet deals about %s shield points an hour (shield %.0f, hull %.0f).", fmtInt((int64_t)(w.fleetStrength(f) * 30)).c_str(),
                        OUTPOST_SHIELD, OUTPOST_HULL);
            } else {
                textDim("Pick an outpost that is not yours.");
            }
            break;
        }
    }
    if (o.kind == OrderKind::Haul || o.kind == OrderKind::Trade) {
        ImGui::Checkbox("Insure the cargo on each trip", &o.insureCargo);
        helpMark("The underwriters price cover on the riskiest system along the route: 0.2% of the cargo's value plus 0.04% per "
                 "risk point. They pay 80% of what is lost.");
    }
    if (o.kind != OrderKind::None) {
        ImGui::Text("Route:");
        ImGui::SameLine();
        int pol = (int)o.policy;
        ImGui::RadioButton("Shortest", &pol, 0); ImGui::SameLine();
        ImGui::RadioButton("Lowest risk", &pol, 1); ImGui::SameLine();
        ImGui::RadioButton("Avoid Frontier", &pol, 2);
        o.policy = (RoutePolicy)pol;
        if (dest != NONE) {
            auto r = w.route(f.system, dest, o.policy, w.player);
            int contract = 0, frontier = 0;
            for (Id s : r) { contract += w.systems[s].zone == Zone::Contract; frontier += w.systems[s].zone == Zone::Frontier; }
            double rr = w.routeRisk(r, f.system);
            textDim("%d jumps to the first stop  ·  worst rating on the way", (int)r.size());
            ImGui::SameLine();
            riskBadge((float)rr);
            if (contract || frontier) { ImGui::SameLine(); textColored(col::Warn, " through %d Contract and %d Frontier systems", contract, frontier); }
        }
    }
    ImGui::Spacing();
    if (accentButton("Issue order", ImVec2(140, 0))) {
        Command c;
        c.type = Cmd::SetFleetOrder;
        c.a = f.id;
        c.order = o;
        run(c);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) d.fleet = NONE;
}

// --------------------------------------------------------- form a fleet ----
void App::formFleetPanel() {
    World& w = world;
    static std::set<Id> picked;
    static char name[48] = "";
    std::map<Id, std::vector<Id>> byStation;
    for (Id sid : w.pc().shipList) {
        const Ship& s = w.ships[sid];
        if (s.alive && s.corp == w.player && s.fleet == NONE) byStation[s.station].push_back(s.id);
    }
    if (byStation.empty()) { textDim("Every ship you own is already in a fleet."); return; }
    for (auto it = picked.begin(); it != picked.end();)
        if (w.ships[*it].fleet != NONE || !w.ships[*it].alive) it = picked.erase(it); else ++it;
    Id pickedStation = picked.empty() ? NONE : w.ships[*picked.begin()].station;
    for (auto& [st, ids] : byStation) {
        textDim("%s", w.stations[st].name.c_str());
        for (Id s : ids) {
            bool on = picked.count(s) > 0;
            ImGui::PushID(s);
            ImGui::BeginDisabled(pickedStation != NONE && pickedStation != st);
            if (ImGui::Checkbox(shipName(w, s).c_str(), &on)) { if (on) picked.insert(s); else picked.erase(s); }
            ImGui::EndDisabled();
            ImGui::SameLine(210);
            textDim("%s  ·  %s", shipDef(w.ships[s].def).cls.c_str(),
                    w.ships[s].pilot == NONE ? "no pilot" : w.people[w.ships[s].pilot].name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##fname", "fleet name (optional)", name, sizeof name);
    ImGui::SameLine();
    ImGui::BeginDisabled(picked.empty());
    if (accentButton("Form fleet")) {
        Command c;
        c.type = Cmd::CreateFleet;
        c.list.assign(picked.begin(), picked.end());
        c.text = name;
        Result r = run(c);
        if (r.ok) { selFleet = r.id; picked.clear(); name[0] = 0; draft.fleet = NONE; }
    }
    ImGui::EndDisabled();
    anchor("form_fleet");
}

void App::winFleets() {
    if (!beginWindow(*this, W_FLEETS, ImVec2(100, 80), ImVec2(1040, 640))) { ImGui::End(); return; }
    World& w = world;
    std::vector<Id> mine;
    for (Id fid : w.pc().fleetList)
        if (w.fleets[fid].alive && w.fleets[fid].corp == w.player) mine.push_back(fid);
    if (selFleet != NONE && (selFleet >= (Id)w.fleets.size() || !w.fleets[selFleet].alive || w.fleets[selFleet].corp != w.player)) selFleet = NONE;
    if (selFleet == NONE && !mine.empty()) selFleet = mine.front();

    ImGui::BeginChild("fl_left", ImVec2(430 * settings.uiScale, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    header("Your fleets");
    if (mine.empty()) textDim("No fleets yet. Form one below from ships in your hangars.");
    else if (beginTable("fleets", 4, 0, ImVec2(0, std::min(320.0f, 30.0f + 26.0f * mine.size())))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Fleet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ships");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (Id id : mine) {
            Fleet& f = w.fleets[id];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(id);
            if (ImGui::Selectable(f.name.c_str(), selFleet == id, ImGuiSelectableFlags_SpanAllColumns)) { selFleet = id; }
            ImGui::PopID();
            ImGui::TableNextColumn(); cellInt((int64_t)f.ships.size());
            ImGui::TableNextColumn();
            if (f.division >= 0) textColored(col::Accent, "%s", divisionName(f.division));
            else ImGui::TextUnformatted(orderKindName(f.order.kind));
            ImGui::TableNextColumn();
            ImU32 c = f.battle != NONE ? col::Bad : f.status.rfind("Needs", 0) == 0 || f.status.rfind("No ", 0) == 0 ? col::Warn : col::Dim;
            textColored(c, "%s", f.battle != NONE ? "IN COMBAT" : f.status.c_str());
        }
        ImGui::EndTable();
    }
    header("Form a fleet");
    formFleetPanel();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("fl_right");
    if (selFleet == NONE) {
        textDim("Select or form a fleet.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    Fleet& f = w.fleets[selFleet];
    ImGui::PushFont(fonts.title, 21.0f);
    ImGui::TextUnformatted(f.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::SmallButton("Rename")) ImGui::OpenPopup("rename");
    if (ImGui::BeginPopup("rename")) {
        static char nb2[48];
        if (ImGui::IsWindowAppearing()) std::snprintf(nb2, sizeof nb2, "%s", f.name.c_str());
        ImGui::InputText("##rn", nb2, sizeof nb2);
        if (ImGui::Button("OK")) { Command c; c.type = Cmd::RenameFleet; c.a = f.id; c.text = nb2; run(c, false); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Show on map")) { centerMap(f.system); selSystem = f.system; }
    ImGui::SameLine();
    if (f.docked != NONE && ImGui::SmallButton("Disband")) { Command c; c.type = Cmd::DisbandFleet; c.a = f.id; run(c); selFleet = NONE; draft.fleet = NONE; }
    if (selFleet == NONE) { ImGui::EndChild(); ImGui::End(); return; }
    // who gives this fleet its orders
    {
        ImGui::SetNextItemWidth(220);
        std::string prev = f.division >= 0 ? std::string("Run by the ") + divisionName(f.division) + " division" : "Run by you";
        if (ImGui::BeginCombo("##rundiv", prev.c_str())) {
            if (ImGui::Selectable("Run by you", f.division < 0)) { Command c; c.type = Cmd::AssignDivision; c.a = f.id; c.b = -1; run(c); }
            for (int d : {DIV_MINING, DIV_TRADE, DIV_HAULAGE, DIV_SECURITY}) {
                std::string l = std::string("Run by the ") + divisionName(d) + " division";
                const Person* p = w.director(w.player, d);
                l += p ? "  (" + p->name + ")" : "  (no director)";
                if (ImGui::Selectable(l.c_str(), f.division == d)) { Command c; c.type = Cmd::AssignDivision; c.a = f.id; c.b = d; run(c); }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (f.division >= 0) {
            const DivisionState& ds = w.pc().divs[f.division];
            if (!ds.pol.automate || !w.director(w.player, f.division)) textColored(col::Warn, "the division is not running: switch it on in DIVS");
            else textDim("a manual order below takes it back");
        } else if (ImGui::SmallButton("Divisions...")) openWin(W_DIVS);
    }

    zoneBadge(w.systems[f.system].zone);
    ImGui::SameLine();
    riskBadge(w.systems[f.system].risk, w.systems[f.system].riskLive);
    ImGui::SameLine();
    ImGui::Text("%s", w.fleetLocation(f).c_str());
    ImGui::SameLine();
    textColored(f.battle != NONE ? col::Bad : col::Accent, "  %s", f.battle != NONE ? "IN COMBAT" : actName(f.act));
    textDim("%s", f.status.c_str());
    if (f.act != Act::Idle && f.actEnd > f.actStart && f.battle == NONE) {
        float k = std::clamp((float)(w.now - f.actStart + frac) / (float)(f.actEnd - f.actStart), 0.0f, 1.0f);
        progressBar(k, ImVec2(220, 0), col::Accent);
        ImGui::SameLine();
        textDim("%s left on this step, ~%s to next stop", fmtDuration(f.actEnd - w.now).c_str(), fmtDuration(w.fleetEta(f)).c_str());
    }
    double cc = w.fleetCargoCap(f), oc = w.fleetOreCap(f);
    double used = w.fleetCargoUsed(f, false), ore = w.fleetCargoUsed(f, true);
    ImGui::Text("Cargo");
    ImGui::SameLine(80);
    progressBar(cc + oc > 0 ? (float)(used / (cc + oc)) : 0, ImVec2(200, 0), col::Accent);
    ImGui::SameLine();
    textDim("%s / %s m3  (ore hold %s, %s ore aboard)", fmtQty(used).c_str(), fmtQty(cc + oc).c_str(), fmtQty(oc).c_str(), fmtQty(ore).c_str());
    textDim("Strength %.0f  ·  %.1f min per hex  ·  evasion %.0f%%  ·  trips %d", w.fleetStrength(f), w.fleetMinutesPerHex(f),
            w.fleetEvasion(f) * 100, f.trips);
    if (f.cargoCover > 0) { ImGui::SameLine(); textColored(col::Good, "  cargo insured for %s", fmtMoneyShort(f.cargoCover).c_str()); }
    {
        const StarSystem& here = w.systems[f.system];
        bool kit = f.cargo.count(OUTPOST_KIT) > 0;
        if (kit && f.docked == NONE && here.zone == Zone::Frontier && here.owner == NONE) {
            if (accentButton("Anchor outpost here")) { Command c; c.type = Cmd::AnchorOutpost; c.a = f.id; run(c); }
            ImGui::SameLine();
            textDim("The kit in the hold can be anchored in %s.", here.name.c_str());
        }
    }

    if (ImGui::BeginTabBar("ftabs")) {
        if (ImGui::BeginTabItem("Orders")) {
            textDim("Current: %s", orderKindName(f.order.kind));
            fleetOrderEditor(f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Ships and crew")) {
            if (beginTable("fships", 5)) {
                ImGui::TableSetupColumn("Ship", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Class");
                ImGui::TableSetupColumn("Pilot");
                ImGui::TableSetupColumn("Hull");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                Id remove = NONE;
                for (Id s : f.ships) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(s + 300000);
                    if (ImGui::Selectable(shipName(w, s).c_str(), selShip == s)) openShip(s);
                    ImGui::PopID();
                    ImGui::TableNextColumn(); textDim("%s", shipDef(w.ships[s].def).cls.c_str());
                    ImGui::TableNextColumn(); pilotCombo(*this, s);
                    ImGui::TableNextColumn();
                    float hf = (float)(w.ships[s].hull / std::max(1.0, w.stats(w.ships[s]).hull));
                    progressBar(hf, ImVec2(80, 0), hf < 0.4f ? col::Bad : col::Good);
                    ImGui::TableNextColumn();
                    ImGui::PushID(s);
                    if (f.docked != NONE && f.ships.size() > 1 && ImGui::SmallButton("Leave fleet")) remove = s;
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (remove != NONE) { Command c; c.type = Cmd::RemoveShip; c.a = f.id; c.b = remove; run(c, false); }
            }
            if (f.docked != NONE) {
                header("Add a ship docked here");
                bool any = false;
                for (Id sid : w.pc().shipList) {
                    const Ship& s = w.ships[sid];
                    if (!s.alive || s.corp != w.player || s.fleet != NONE || s.station != f.docked) continue;
                    any = true;
                    ImGui::PushID(s.id);
                    if (ImGui::SmallButton("Add")) { Command c; c.type = Cmd::AddShip; c.a = f.id; c.b = s.id; run(c, false); }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(shipName(w, s.id).c_str());
                    ImGui::PopID();
                }
                if (!any) textDim("No idle ships at this station.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cargo")) {
            if (f.cargo.empty()) textDim("Empty.");
            else if (beginTable("fcargo", 3)) {
                ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Quantity");
                ImGui::TableSetupColumn("Volume m3");
                ImGui::TableHeadersRow();
                for (auto& [it, q] : f.cargo) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(item(it).name.c_str());
                    ImGui::TableNextColumn(); cellInt(q);
                    ImGui::TableNextColumn(); cellQty(q * item(it).volume);
                }
                ImGui::EndTable();
            }
            textDim("Cargo is unloaded into the hangar when a Mine, Haul or Trade order arrives, or when you disband the fleet.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------ assets --
void App::winAssets() {
    if (!beginWindow(*this, W_ASSETS, ImVec2(110, 100), ImVec2(820, 560))) { ImGui::End(); return; }
    World& w = world;
    if (ImGui::BeginTabBar("atabs")) {
        if (ImGui::BeginTabItem("Hangars")) {
            std::map<Id, std::vector<std::pair<int, int64_t>>> by;
            for (auto& [k, q] : w.pc().hangar) by[(Id)(k / HANGAR_KEY)].push_back({(int)(k % HANGAR_KEY), q});
            for (Id sid : w.pc().shipList) {
                const Ship& s = w.ships[sid];
                if (s.alive && s.corp == w.player && s.fleet == NONE) {
                    auto& v = by[s.station];
                    bool found = false;
                    for (auto& [it, q] : v)
                        if (it == SHIP_FIRST + s.def) { q++; found = true; }
                    if (!found) v.push_back({SHIP_FIRST + s.def, 1});
                }
            }
            if (by.empty()) textDim("Your hangars are empty.");
            if (!by.empty() && beginTable("hangars", 5)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Station / item", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Quantity");
                ImGui::TableSetupColumn("Volume m3");
                ImGui::TableSetupColumn("Est. value");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (auto& [st, items] : by) {
                    Money tot = 0;
                    double vol = 0;
                    for (auto& [it, q] : items) { tot += w.valueStock(st, it, q); vol += q * item(it).volume; }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(st);
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    bool openNode = ImGui::TreeNodeEx(w.stations[st].name.c_str(), ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn(); cellQty(vol);
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(tot, false), col::Accent);
                    ImGui::TableNextColumn();
                    if (openNode) {
                        for (auto& [it, q] : items) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Indent();
                            ImGui::TextUnformatted(item(it).name.c_str());
                            ImGui::Unindent();
                            ImGui::TableNextColumn(); cellInt(q);
                            ImGui::TableNextColumn(); cellQty(q * item(it).volume);
                            ImGui::TableNextColumn(); cellMoney(w.valueStock(st, it, q));
                            ImGui::TableNextColumn();
                            ImGui::PushID(it);
                            if (ImGui::SmallButton("Market")) showMarket(st, it, 0);
                            if (isOre(it)) { ImGui::SameLine(); if (ImGui::SmallButton("Refine")) { selStation = st; openWin(W_INDUSTRY); } }
                            if (isModule(it)) { ImGui::SameLine(); if (ImGui::SmallButton("Fit")) openWin(W_SHIP); }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Ships")) {
            if (beginTable("ships", 8)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Ship", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Class");
                ImGui::TableSetupColumn("Where");
                ImGui::TableSetupColumn("Pilot");
                ImGui::TableSetupColumn("Hull");
                ImGui::TableSetupColumn("Insured");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Fitted");
                ImGui::TableHeadersRow();
                for (Id sid : w.pc().shipList) {
                    const Ship& s = w.ships[sid];
                    if (!s.alive || s.corp != w.player) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(sid + 200000);
                    if (ImGui::Selectable(shipName(w, s.id).c_str(), selShip == sid)) openShip(sid);
                    ImGui::PopID();
                    ImGui::TableNextColumn(); textDim("%s", shipDef(s.def).cls.c_str());
                    ImGui::TableNextColumn();
                    if (s.fleet != NONE) textColored(col::Accent, "%s", w.fleets[s.fleet].name.c_str());
                    else ImGui::TextUnformatted(w.stations[s.station].name.c_str());
                    ImGui::TableNextColumn(); pilotCombo(*this, s.id);
                    ImGui::TableNextColumn();
                    float hf = (float)(s.hull / std::max(1.0, w.stats(s).hull));
                    progressBar(hf, ImVec2(60, 0), hf < 0.4f ? col::Bad : col::Good);
                    ImGui::TableNextColumn();
                    ImGui::PushID(s.id + 100000);
                    bool ins = s.insured && s.insuredUntil > w.now;
                    if (ins) textColored(col::Good, "%s", fmtDuration(s.insuredUntil - w.now).c_str());
                    else if (ImGui::SmallButton("Insure")) { Command c; c.type = Cmd::SetInsurance; c.a = s.id; c.flag = true; run(c); }
                    ImGui::PopID();
                    ImGui::TableNextColumn(); cellMoney(w.shipValue(s));
                    ImGui::TableNextColumn();
                    int used = 0;
                    for (int k = 0; k < shipDef(s.def).slots; k++) used += s.fit[k] >= 0;
                    rightText(std::to_string(used) + "/" + std::to_string(shipDef(s.def).slots), used ? col::Accent : col::Dim);
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
