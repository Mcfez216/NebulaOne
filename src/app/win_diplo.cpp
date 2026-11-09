// NebulaOne - Diplomacy (wars, pacts, mercenaries) and Intel and Exploration
// (sensors, brokers, discovered sites, market snapshots).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static std::string ago(const World& w, Tick t) { return t < 0 ? "never" : w.now - t < 60 ? "just now" : fmtDuration(w.now - t) + " ago"; }

// A searchable list of every other living corporation.
static bool anyCorpPicker(App& app, const char* id, Id& corp, float width, bool (*keep)(const World&, const Corp&)) {
    World& w = app.world;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    std::string preview = corp == NONE ? "(choose a corporation)" : w.corps[corp].name + "  [" + w.corps[corp].ticker + "]";
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char buf[48] = "";
        if (ImGui::IsWindowAppearing()) { buf[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##f", "filter by name, ticker or type", buf, sizeof buf);
        std::string f = upper(buf);
        Id homeSys = w.stations[w.pc().home].system;
        std::vector<Id> ids;
        for (auto& c : w.corps) {
            if (!c.alive || c.player || (keep && !keep(w, c))) continue;
            if (!f.empty() && upper(c.name + " " + c.ticker + " " + archName(c.arch)).find(f) == std::string::npos) continue;
            ids.push_back(c.id);
        }
        std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
            int da = hexDistance(w.systems[homeSys].hex, w.sysOf(w.corps[a].home).hex), db = hexDistance(w.systems[homeSys].hex, w.sysOf(w.corps[b].home).hex);
            return da != db ? da < db : a < b;
        });
        if (ids.size() > 250) ids.resize(250);
        for (Id cid : ids) {
            const Corp& c = w.corps[cid];
            ImGui::PushID(cid);
            const char* rel = w.atWar(cid, w.player) ? "WAR" : w.allied(cid, w.player) ? "ALLY" : w.nonAggression(cid, w.player) ? "PACT" : "";
            if (ImGui::Selectable((c.name + "  [" + c.ticker + "]").c_str(), corp == cid)) { corp = cid; changed = true; }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 190);
            textDim("%-13s %+5.1f  %s", archName(c.arch), w.standing(cid, w.player), rel);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

static bool notOutlaw(const World&, const Corp& c) { return c.arch != Arch::Pirate && c.arch != Arch::Ganker; }
static bool isMerc(const World&, const Corp& c) { return c.arch == Arch::Mercenary; }

void App::winDiplomacy() {
    if (!beginWindow(*this, W_DIPLO, ImVec2(160, 90), ImVec2(860, 680))) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(0);
    World& w = world;
    Id me = w.player;
    int wars = 0, pending = 0, allies = 0, pacts = 0, inbox = 0;
    for (auto& t : w.treaties) {
        if (!t.active || (t.a != me && t.b != me)) continue;
        if (t.kind == Pact::War) (t.live(w.now) ? wars : pending)++;
        if (t.kind == Pact::Alliance) allies++;
        if (t.kind == Pact::NonAggression) pacts++;
    }
    for (auto& o : w.offers) inbox += o.open && o.to == me && o.expires > w.now;
    if (ImGui::BeginTable("dk", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("At war", std::to_string(wars) + (pending ? " (+" + std::to_string(pending) + " pending)" : ""), wars ? col::Bad : col::Text);
        ImGui::TableNextColumn(); kpi("Allies", std::to_string(allies), allies ? col::Good : col::Text);
        ImGui::TableNextColumn(); kpi("Non-aggression pacts", std::to_string(pacts));
        ImGui::TableNextColumn(); kpi("Offers waiting", std::to_string(inbox), inbox ? col::Warn : col::Text);
        ImGui::EndTable();
    }
    if (ImGui::BeginTabBar("diptabs")) {
        if (ImGui::BeginTabItem("Your affairs")) {
            header("Offers for you");
            int shown = 0;
            for (auto& o : w.offers) {
                if (!o.open || o.to != me || o.expires <= w.now) continue;
                shown++;
                ImGui::PushID(o.id);
                textColored(o.kind == OfferKind::JoinWar ? col::Warn : col::Text, "%s", o.note.c_str());
                textDim("%s from %s  ·  expires in %s", offerKindName(o.kind), w.corps[o.from].name.c_str(), fmtDuration(o.expires - w.now).c_str());
                ImGui::SameLine();
                if (accentButton("Accept")) { Command c; c.type = Cmd::RespondOffer; c.a = o.id; c.flag = true; run(c); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Decline")) { Command c; c.type = Cmd::RespondOffer; c.a = o.id; c.flag = false; run(c); }
                ImGui::PopID();
            }
            if (!shown) textDim("Nothing waiting. Rivals send peace terms, pacts and calls to arms here.");

            header("Wars");
            int nw = 0;
            static Money peaceAmt = 0;
            for (auto& t : w.treaties) {
                if (!t.active || t.kind != Pact::War || (t.a != me && t.b != me && t.hiredBy != me)) continue;
                nw++;
                ImGui::PushID(t.id);
                bool ours = t.a == me || t.b == me;
                Id enemy = t.a == me ? t.b : t.b == me ? t.a : t.b;
                Money mine = t.a == me ? t.scoreA : t.scoreB, theirs = t.a == me ? t.scoreB : t.scoreA;
                if (!ours) { mine = t.scoreA; theirs = t.scoreB; }
                textColored(col::Bad, "%s", ours ? ("War with " + w.corps[enemy].name).c_str()
                                                  : (w.corps[t.a].name + " (hired) against " + w.corps[t.b].name).c_str());
                ImGui::SameLine();
                if (t.start > w.now) textColored(col::Warn, "starts in %s", fmtDuration(t.start - w.now).c_str());
                else textDim("ends in %s", fmtDuration(t.until - w.now).c_str());
                textDim("Destroyed: %s by %s, %s by them%s%s", fmtMoneyShort(mine).c_str(), ours ? "you" : w.corps[t.a].ticker.c_str(),
                        fmtMoneyShort(theirs).c_str(), t.onBehalf != NONE ? ("  ·  in defence of " + w.corps[t.onBehalf].name).c_str() : "",
                        t.why.empty() ? "" : ("  ·  " + t.why).c_str());
                if (ours) {
                    if (t.start > w.now && t.a == me) {
                        if (ImGui::SmallButton("Withdraw the declaration")) { Command c; c.type = Cmd::CancelPact; c.a = t.id; run(c); }
                    } else {
                        inputCredits("##pa", peaceAmt, 140);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Offer peace, paying this")) { Command c; c.type = Cmd::ProposePeace; c.a = enemy; c.price = peaceAmt; run(c); }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Demand this for peace")) { Command c; c.type = Cmd::ProposePeace; c.a = enemy; c.price = -peaceAmt; run(c); }
                        ImGui::SameLine();
                        helpMark("Rivals that are winning want reparations; beaten ones may pay you. A white peace (0) works once both sides "
                                 "have had enough. Wars also end when the 14-day licence runs out.");
                    }
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (!nw) textDim("At peace. A war licence makes fighting a rival lawful everywhere, even under the Wardens' noses.");

            header("Pacts");
            int np = 0;
            for (auto& t : w.treaties) {
                if (!t.active || t.kind == Pact::War || (t.a != me && t.b != me)) continue;
                np++;
                ImGui::PushID(t.id + 100000);
                Id other = t.a == me ? t.b : t.a;
                textColored(t.kind == Pact::Alliance ? col::Good : col::Text, "%s with %s", pactName(t.kind), w.corps[other].name.c_str());
                ImGui::SameLine();
                if (t.until - w.now < 2 * MINUTES_PER_DAY) textColored(col::Warn, "ends in %s", fmtDuration(t.until - w.now).c_str());
                else {
                    textDim("since %s", fmtDate(t.start).substr(0, 10).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Give notice")) { Command c; c.type = Cmd::CancelPact; c.a = t.id; run(c); }
                }
                ImGui::PopID();
            }
            if (!np) textDim("No pacts. Allies share their sensors with you and come to your defence; pact partners never raid you.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Make a move")) {
            static Id target = NONE;
            anyCorpPicker(*this, "##dt", target, 420, notOutlaw);
            if (target != NONE && (!w.corps[target].alive || w.corps[target].player)) target = NONE;
            if (target != NONE) {
                const Corp& t = w.corps[target];
                double ms = w.corpStrength(me), ts = w.corpStrength(target);
                ImGui::PushFont(fonts.title, 19.0f);
                ImGui::Text("%s  [%s]", t.name.c_str(), t.ticker.c_str());
                ImGui::PopFont();
                textDim("%s  ·  net worth %s  ·  home %s  ·  their standing towards you %+.1f", archName(t.arch), fmtMoneyShort(t.netWorth).c_str(),
                        w.stations[t.home].name.c_str(), w.standing(target, me));
                textDim("Combat strength: theirs %.0f, yours %.0f", ts, ms);
                ImGui::Spacing();
                if (w.atWar(me, target) || w.findTreaty(Pact::War, me, target, false)) textColored(col::Bad, "You are at war with them: see Your affairs.");
                else {
                    Money fee = w.warFee(target);
                    if (accentButton(("Declare war (" + fmtMoneyShort(fee) + " licence)").c_str())) { Command c; c.type = Cmd::DeclareWar; c.a = target; run(c); }
                    ImGui::SameLine();
                    helpMark("The Charter Authority's war licence: from 5M cr, or 1.5% of the target's net worth. Hostilities start a day "
                             "later and run 14 days. While it runs, fighting them is lawful everywhere and their allies may join in.");
                    ImGui::SameLine();
                    if (ImGui::Button("Propose a non-aggression pact")) { Command c; c.type = Cmd::ProposePact; c.a = target; c.flag = false; run(c); }
                    ImGui::SameLine();
                    if (ImGui::Button("Propose an alliance")) { Command c; c.type = Cmd::ProposePact; c.a = target; c.flag = true; run(c); }
                    textDim("They sign a pact at standing 0 or better; an alliance at 6, or 1 if you share an enemy. Contracts and repaid "
                            "loans raise standing.");
                }
            } else textDim("Pick a corporation to deal with.");

            header("Hire mercenaries");
            static Id merc = NONE, mtarget = NONE;
            static int days = 7;
            anyCorpPicker(*this, "Mercenary company", merc, 360, isMerc);
            anyCorpPicker(*this, "Against", mtarget, 360, notOutlaw);
            ImGui::SetNextItemWidth(200);
            ImGui::SliderInt("days", &days, 3, 30);
            if (merc != NONE && w.corps[merc].alive) {
                Money q = w.mercQuote(merc, days);
                textDim("%s fields %.0f combat strength. They want %s for %d days (%s a day).", w.corps[merc].name.c_str(), w.corpStrength(merc),
                        fmtMoneyShort(q).c_str(), days, fmtMoneyShort(q / days).c_str());
                ImGui::BeginDisabled(mtarget == NONE);
                if (accentButton("Hire them")) { Command c; c.type = Cmd::HireMercs; c.a = merc; c.b = mtarget; c.qty = days; run(c); }
                ImGui::EndDisabled();
                ImGui::SameLine();
                textDim("They take out their own licence and send their fleets after the target.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("The galaxy")) {
            textDim("Every licensed war on the chart, and who is winning it.");
            if (beginTable("gw", 6, ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Aggressor", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Destroyed by aggressor");
                ImGui::TableSetupColumn("Destroyed by target");
                ImGui::TableSetupColumn("Ends in");
                ImGui::TableSetupColumn("Why");
                ImGui::TableHeadersRow();
                for (auto& t : w.treaties) {
                    if (!t.active || t.kind != Pact::War) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); textColored(t.a == me ? col::Accent : col::Text, "%s", w.corps[t.a].name.c_str());
                    ImGui::TableNextColumn(); textColored(t.b == me ? col::Accent : col::Text, "%s", w.corps[t.b].name.c_str());
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(t.scoreA, false));
                    ImGui::TableNextColumn(); rightText(fmtMoneyShort(t.scoreB, false));
                    ImGui::TableNextColumn(); textDim("%s", t.start > w.now ? "pending" : fmtDuration(t.until - w.now).c_str());
                    ImGui::TableNextColumn(); textDim("%s", t.why.c_str());
                }
                ImGui::EndTable();
            }
            int al = 0, na = 0;
            for (auto& t : w.treaties)
                if (t.active && t.kind != Pact::War) (t.kind == Pact::Alliance ? al : na)++;
            textDim("%d alliances and %d non-aggression pacts in force across the chart.", al, na);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

void App::winIntel() {
    if (!beginWindow(*this, W_INTEL, ImVec2(180, 100), ImVec2(820, 640))) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(0);
    World& w = world;
    Id homeSys = w.stations[w.pc().home].system;
    int liveSys = 0, sightSys = 0;
    for (auto v : w.sight) { liveSys += v >= 2; sightSys += v >= 1; }
    int mySites = 0, epochSurveys = 0;
    for (auto& s : w.sites) mySites += s.finder == w.player && !s.done && s.expires > w.now;
    for (auto& [sy, e] : w.surveyed) epochSurveys += e == w.fortnight();
    if (ImGui::BeginTable("ik", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Fog of war", w.fog ? "On" : "Off");
        ImGui::TableNextColumn(); kpi("Systems in live sight", std::to_string(liveSys) + " / " + std::to_string(w.systems.size()));
        ImGui::TableNextColumn(); kpi("Open sites", std::to_string(mySites), mySites ? col::Accent : col::Text);
        ImGui::TableNextColumn(); kpi("Surveyed this fortnight", std::to_string(epochSurveys));
        ImGui::EndTable();
    }
    if (ImGui::BeginTabBar("inttabs")) {
        if (ImGui::BeginTabItem("Sites")) {
            textDim("What your surveys have found. Derelicts and relic caches are salvaged with any ship that has hold space; ore pockets are "
                    "mined like any belt, but only you know they are there; drift lanes are shortcuts only your fleets can use.");
            if (beginTable("sites", 7, ImGuiTableFlags_Sortable, ImVec2(0, 260))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("System");
                ImGui::TableSetupColumn("Zone");
                ImGui::TableSetupColumn("Jumps");
                ImGui::TableSetupColumn("Closes in");
                ImGui::TableSetupColumn("Status");
                ImGui::TableHeadersRow();
                for (auto& s : w.sites) {
                    if (s.finder != w.player) continue;
                    bool open = !s.done && s.expires > w.now;
                    ImGui::TableNextRow();
                    ImGui::PushID(s.id);
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(s.name.c_str(), selSite == s.id, ImGuiSelectableFlags_SpanAllColumns)) { selSite = s.id; selectSystem(s.system, false); centerMap(s.system); }
                    ImGui::TableNextColumn(); textDim("%s", siteKindName(s.kind));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(w.systems[s.system].name.c_str());
                    ImGui::TableNextColumn(); zoneBadge(w.systems[s.system].zone);
                    ImGui::TableNextColumn(); cellInt(w.jumps(homeSys, s.system));
                    ImGui::TableNextColumn(); textDim("%s", open ? fmtDuration(s.expires - w.now).c_str() : "-");
                    ImGui::TableNextColumn();
                    if (open) textColored(col::Good, "open");
                    else textDim(s.kind == SiteKind::Drift || s.kind == SiteKind::Pocket ? "closed" : "salvaged");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            const Site* sel = nullptr;
            for (auto& s : w.sites)
                if (s.id == selSite) sel = &s;
            if (sel && sel->finder == w.player) {
                header(sel->name.c_str());
                const StarSystem& sy = w.systems[sel->system];
                textDim("%s in %s (%s), level %d. Found %s.", siteKindName(sel->kind), sy.name.c_str(), sy.hexId.c_str(), sel->level,
                        fmtDate(sel->found).c_str());
                bool open = !sel->done && sel->expires > w.now;
                if (open && (sel->kind == SiteKind::Derelict || sel->kind == SiteKind::Relic)) {
                    static Id sf = NONE;
                    ImGui::SetNextItemWidth(260);
                    std::string pv = sf == NONE || !w.fleets[sf].alive ? "(choose a fleet)" : w.fleets[sf].name;
                    if (ImGui::BeginCombo("Fleet", pv.c_str())) {
                        for (Id fid : w.pc().fleetList) {
                            const Fleet& f = w.fleets[fid];
                            if (!f.alive) continue;
                            ImGui::PushID(fid);
                            if (ImGui::Selectable((f.name + "  (" + fmtQty(w.fleetCargoCap(f) - w.fleetCargoUsed(f, false)) + " m3 free)").c_str(), sf == fid)) sf = fid;
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(sf == NONE);
                    if (accentButton("Salvage it")) {
                        Command c; c.type = Cmd::SetFleetOrder; c.a = sf; c.order.kind = OrderKind::Salvage; c.order.site = sel->id;
                        c.order.station = w.pc().home; c.order.policy = RoutePolicy::LowestRisk;
                        run(c);
                    }
                    ImGui::EndDisabled();
                    textDim("The fleet flies out, works the site for %s, then brings what it can carry to your home station.",
                            sel->kind == SiteKind::Relic ? "5 hours" : "3 hours");
                }
                if (open && sel->kind == SiteKind::Pocket && sel->belt >= 0) {
                    const Belt& b = sy.belts[sel->belt];
                    textDim("%s of %s left. Give a mining fleet a Mine order on belt %d in %s.", fmtQty(b.remaining).c_str(), item(b.ore).name.c_str(),
                            sel->belt + 1, sy.name.c_str());
                    if (ImGui::SmallButton("Open the system")) selectSystem(sel->system);
                }
                if (open && sel->kind == SiteKind::Drift) {
                    textDim("A %d-hex lane from %s to %s (%d hexes apart as the crow flies). Your routes use it automatically.", sel->hexes,
                            sy.name.c_str(), w.systems[sel->link].name.c_str(), hexDistance(sy.hex, w.systems[sel->link].hex));
                    if (ImGui::SmallButton("Show the far end")) { selectSystem(sel->link); centerMap(sel->link); }
                }
            }
            header("Survey");
            textDim("Any fleet can survey; a Wisp, or a Survey Scanner, works faster and finds more. Every system hides new sites each fortnight, "
                    "first come first served: rivals survey too. Richer sites lie deeper in Contract space and the Frontier.");
            static Id svf = NONE;
            static bool loop = true;
            ImGui::SetNextItemWidth(260);
            std::string pv = svf == NONE || !w.fleets[svf].alive ? "(choose a fleet)" : w.fleets[svf].name;
            if (ImGui::BeginCombo("Survey fleet", pv.c_str())) {
                for (Id fid : w.pc().fleetList) {
                    const Fleet& f = w.fleets[fid];
                    if (!f.alive) continue;
                    double scan = 0;
                    for (Id s : f.ships) scan = std::max(scan, w.stats(w.ships[s]).scan);
                    ImGui::PushID(fid);
                    char lab[160];
                    std::snprintf(lab, sizeof lab, "%s  (survey %.2f)", f.name.c_str(), scan);
                    if (ImGui::Selectable(lab, svf == fid)) svf = fid;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            Id tsys = selSystem != NONE ? selSystem : homeSys;
            ImGui::SameLine();
            textDim("target: %s", w.systems[tsys].name.c_str());
            ImGui::Checkbox("Keep going: survey the next unsurveyed system within 3 jumps", &loop);
            ImGui::BeginDisabled(svf == NONE);
            if (accentButton(("Survey " + w.systems[tsys].name).c_str())) {
                Command c; c.type = Cmd::SetFleetOrder; c.a = svf; c.order.kind = OrderKind::Survey; c.order.system = tsys; c.order.loop = loop;
                c.order.policy = RoutePolicy::LowestRisk;
                run(c);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            textDim("(select a system on the chart to change the target)");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sensors and brokers")) {
            if (!w.fog) textDim("Fog of war is off in this game: every market and fleet is in plain view.");
            textDim("You see live markets and fleets around your headquarters (one jump), your fleets (a jump with a survey array, two with a "
                    "Wisp and a Survey Scanner, one more on a Scout order), your outposts (two jumps) and your allies' fleets and outposts. "
                    "The Charter Exchange publishes its hubs' prices at each day's close.");
            header("Intel brokers");
            static Id reg = NONE;
            if (reg == NONE) reg = w.systems[homeSys].region;
            ImGui::SetNextItemWidth(320);
            if (ImGui::BeginCombo("Region", w.regions[reg].name.c_str(), ImGuiComboFlags_HeightLarge)) {
                std::vector<std::pair<int, Id>> order;
                for (size_t r = 0; r < w.regions.size(); r++)
                    if (!w.regionSystems[r].empty())
                        order.push_back({hexDistance(w.systems[homeSys].hex, w.systems[w.regionSystems[r][0]].hex), (Id)r});
                std::sort(order.begin(), order.end());
                if (order.size() > 200) order.resize(200);
                for (auto& [d, r] : order) {
                    ImGui::PushID(r);
                    if (ImGui::Selectable(w.regions[r].name.c_str(), reg == r)) reg = r;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - 120);
                    textDim("%s  %d systems", zoneName(w.regions[r].zone), (int)w.regionSystems[r].size());
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!w.fog);
            if (ImGui::Button(("Market report: " + fmtMoneyShort(w.intelPrice(reg, false))).c_str())) { Command c; c.type = Cmd::BuyIntel; c.a = reg; c.flag = false; run(c); }
            ImGui::SameLine();
            textDim("every station's prices in the region, as of now");
            if (ImGui::Button(("Fleet sightings for 24 hours: " + fmtMoneyShort(w.intelPrice(reg, true))).c_str())) { Command c; c.type = Cmd::BuyIntel; c.a = reg; c.flag = true; run(c); }
            ImGui::SameLine();
            textDim("every fleet in the region shows on your chart");
            ImGui::EndDisabled();
            for (size_t r = 0; r < w.regionReveal.size(); r++)
                if (w.regionReveal[r] > w.now) textColored(col::Accent, "Sightings from %s for another %s", w.regions[r].name.c_str(), fmtDuration(w.regionReveal[r] - w.now).c_str());
            header("Your sensors");
            for (Id fid : w.pc().fleetList) {
                const Fleet& f = w.fleets[fid];
                if (!f.alive) continue;
                double scan = 0;
                for (Id s : f.ships) scan = std::max(scan, w.stats(w.ships[s]).scan);
                int radius = scan >= 1.6 ? 2 : scan >= 0.8 ? 1 : 0;
                if (f.order.kind == OrderKind::Scout && f.system == f.order.system) radius = std::min(3, radius + 1);
                textDim("%-24s %-20s sees %d jump%s", f.name.c_str(), w.systems[f.system].name.c_str(), radius, radius == 1 ? "" : "s");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Market intel")) {
            textDim("Every market you hold a picture of, and how old it is. Live markets update hourly while your sensors cover them.");
            if (beginTable("mi", 5, ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Station", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Zone");
                ImGui::TableSetupColumn("Jumps");
                ImGui::TableSetupColumn("Prices as of", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("Items");
                ImGui::TableHeadersRow();
                std::vector<std::pair<Id, const MarketSnap*>> rows;
                for (auto& [st, m] : w.intel) rows.push_back({st, &m});
                int sc = 3;
                bool asc = true;
                sortSpec(sc, asc);
                std::sort(rows.begin(), rows.end(), [&](auto& a, auto& b) {
                    double va = 0, vb = 0;
                    switch (sc) {
                        case 0: return asc ? w.stations[a.first].name < w.stations[b.first].name : w.stations[a.first].name > w.stations[b.first].name;
                        case 1: va = (int)w.sysOf(a.first).zone; vb = (int)w.sysOf(b.first).zone; break;
                        case 2: va = w.jumps(homeSys, w.stations[a.first].system); vb = w.jumps(homeSys, w.stations[b.first].system); break;
                        case 3: va = -(double)a.second->t; vb = -(double)b.second->t; break;
                        case 4: va = (double)a.second->rows.size(); vb = (double)b.second->rows.size(); break;
                    }
                    return asc ? va < vb : va > vb;
                });
                ImGuiListClipper clip;
                clip.Begin((int)rows.size());
                while (clip.Step())
                    for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                        Id st = rows[k].first;
                        const MarketSnap& m = *rows[k].second;
                        ImGui::TableNextRow();
                        ImGui::PushID(st);
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(w.stations[st].name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) showMarket(st, -1, 0);
                        ImGui::TableNextColumn(); zoneBadge(w.sysOf(st).zone);
                        ImGui::TableNextColumn(); cellInt(w.jumps(homeSys, w.stations[st].system));
                        ImGui::TableNextColumn();
                        if (w.seen(w.stations[st].system)) textColored(col::Good, "live");
                        else textColored(w.now - m.t > 2 * MINUTES_PER_DAY ? col::Warn : col::Dim, "%s", ago(w, m.t).c_str());
                        ImGui::TableNextColumn(); cellInt((int64_t)m.rows.size());
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

}  // namespace nb
