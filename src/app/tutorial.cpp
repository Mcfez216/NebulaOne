// NebulaOne - the guided tutorial. Each step waits for the player to do the
// thing, points at where to do it, and can be skipped.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/theme.h"
#include "app/widgets.h"

namespace nb {

namespace {
struct Step {
    const char* title;
    std::string body;
    const char* target;  // anchor to highlight
    bool needsAction;
    std::string waiting;  // what we are waiting for
};
}  // namespace

static bool hasFleetWith(const World& w, OrderKind k) {
    for (auto& f : w.fleets)
        if (f.alive && f.corp == w.player && f.order.kind == k) return true;
    return false;
}

static std::vector<Step> steps(App& a) {
    World& w = a.world;
    const Station& home = w.stations[w.pc().home];
    const StarSystem& hs = w.systems[home.system];
    std::string homeName = home.name, sysName = hs.name;
    return {
        {"Welcome, CEO",
         "You run " + w.pc().name + ". You will never fly a ship: you give orders, hire people and move money.\n\n"
         "Behind every window is the live sector chart: one system per hex. Drag to pan, scroll to zoom, click a system for "
         "details. Slate blue is Charter space, where the Wardens keep order; ochre is Contract space, with no police; rust "
         "is the Frontier.\n\nFog of war is on: you see live markets and fleets only around your headquarters and your fleets. "
         "Everywhere else you trade on the last prices you saw.\n\nThe clock is paused. Nothing happens until you let time run.",
         nullptr, false, ""},
        {"Your corporation",
         "Every window has a mnemonic on the function strip under the header. Press CORP. It shows your cash, net worth, "
         "ships and staff at a glance. (You can also type codes into the GO> line at the right, like a terminal.)",
         "rail_corp", true, "Open the Corporation window"},
        {"Your assets",
         "Everything you own sits in a hangar at a specific station. Open ASST and find your Mattock mining "
         "frigate and Drover hauler at " + homeName + ". Click a ship to open its Ship Information and fittings.",
         "rail_assets", true, "Open the Assets window"},
        {"Read a market",
         "Open the Market (MKT, or press M) and look at Cindrite at " + homeName + ". Sell orders are what you can buy "
         "now; buy orders are what you can sell into.\n\nThen open the All stations tab: every station has its own price, "
         "and the gaps between them are a business.",
         "rail_market", true, "View Cindrite, then the All stations tab"},
        {"Form a mining fleet",
         "Open Fleets (FLT, or press F). In Form a fleet, tick the Mattock and press Form fleet; your idle pilots are "
         "assigned automatically.\n\nThen set its order to Mine: pick a belt in " + sysName + " or next door and unload at " +
             homeName + ". Press Issue order.",
         "rail_fleets", true, "A fleet with a Mine order"},
        {"Let time run",
         "Use the speed buttons at the top right (or keys 1 to 5). Watch your fleet leave the station on the chart, mine and "
         "come home.\n\nThis step finishes when the first ore lands in your hangar.",
         "speed", true, "Ore unloaded at a station"},
        {"Refine the ore",
         "Open Industry (IND). On the Refine tab choose " + homeName + ", pick the ore and press Start refining. "
         "Charter refineries return 55% of the minerals, Contract stations 62% and outposts 70%; good staff raise that.",
         "rail_industry", true, "Start a refining job"},
        {"Sell",
         "When the job finishes, open the Market for Ferrox at your station. Sell into the best buy order, or post your own "
         "sell order higher and wait (you pay a 2.5% broker fee to post).",
         "rail_market", true, "Make a sale or post an order"},
        {"Haul for profit",
         "Your Drover can trade. In Fleets, form a fleet with it and choose a Trade run: buy Ration Packs where they are "
         "cheap (an Agri Dome) and sell where they are scarce. The order editor estimates the profit per trip.\n\n"
         "The Market's All stations tab is the spreadsheet for finding those gaps.",
         "rail_fleets", true, "A fleet with a Trade run order"},
        {"Read the danger",
         "Switch the chart key at the bottom left to RISK. Underwriters grade every system from AAA to D, and re-rate "
         "them weekly from what actually happens there. Insurance is priced on the grade.\n\n"
         "The richest ore lies outside the Charter, where the Wardens do not go. Red dots are pirate fleets: they hunt slow, "
         "full haulers. Escort valuable cargo, insure hulls and cargo, and use the Lowest-risk route policy when it matters.",
         "map_legend", false, ""},
        {"Money tools",
         "Open Finance (FIN). Borrow from the Consolidated Bank to grow faster, lend to other corporations on the "
         "Loan Board, and buy shares of your rivals on the Exchange. The Contract Board (JOBS) pays for supply runs, "
         "courier work, bounties and patrols.",
         "rail_finance", true, "Open the Finance window"},
        {"The universe is yours",
         "That is the whole loop: mine, refine, sell, haul, grow. Then the long game:\n\n"
         "  -  Build components, modules and hulls from blueprints, and research them\n"
         "  -  Anchor an outpost in the Frontier and tax everyone who uses it\n"
         "  -  Float your company on the Exchange, and take over rivals\n\n"
         "Help (F1) has the controls and this tutorial if you need them again.",
         nullptr, false, ""},
    };
}

static bool stepDone(App& a, int i) {
    World& w = a.world;
    switch (i) {
        case 1: return a.open[W_CORP];
        case 2: return a.open[W_ASSETS];
        case 3: return a.open[W_MARKET] && a.tut.marketAllSeen;
        case 4: return hasFleetWith(w, OrderKind::Mine);
        case 5: return w.pstats.oreUnloaded > 0;
        case 6: return w.pstats.refines > 0;
        case 7: return w.pstats.sales > 0 || w.pstats.ordersPlaced > 0;
        case 8: return hasFleetWith(w, OrderKind::Trade) || w.pstats.tradeRuns > 0;
        case 10: return a.open[W_FINANCE];
        default: return false;
    }
}

void App::drawTutorial() {
    if (!tut.active) return;
    auto st = steps(*this);
    if (tut.step >= (int)st.size()) { tut.active = false; return; }
    const Step& s = st[tut.step];
    bool done = s.needsAction && stepDone(*this, tut.step);
    // highlight the target
    if (s.target && !done) {
        auto it = anchors.find(s.target);
        if (it != anchors.end()) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            float pulse = 0.5f + 0.5f * std::sin((float)time * 4.0f);
            ImVec4 r = it->second;
            float g = 3 + pulse * 3;
            fg->AddRect(ImVec2(r.x - g, r.y - g), ImVec2(r.z + g, r.w + g), withAlpha(col::Accent, 0.5f + 0.5f * pulse), 3, 0, 2.0f);
        }
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = 400 * settings.uiScale;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - w - 18, vp->Pos.y + vp->Size.y - 40 * settings.uiScale), ImGuiCond_Always,
                            ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(9, 15, 22, 246));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(col::Accent, 0.55f));
    ImGui::Begin("##tutorial", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleColor(2);
    ImGui::PushFont(nullptr, kFontSmall);
    textColored(col::Accent, "TUTORIAL  %d / %d", tut.step + 1, (int)st.size());
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 50);
    if (ImGui::SmallButton(tut.minimized ? "Show" : "Hide")) tut.minimized = !tut.minimized;
    ImGui::PushFont(fonts.bold, 19.0f);
    ImGui::TextUnformatted(s.title);
    ImGui::PopFont();
    if (!tut.minimized) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(s.body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (s.needsAction) {
        if (done) textColored(col::Good, "Done: %s", s.waiting.c_str());
        else textColored(col::Warn, "To do: %s", s.waiting.c_str());
    }
    ImGui::Spacing();
    if (tut.step > 0 && ImGui::Button("Back")) tut.step--;
    ImGui::SameLine();
    if (ImGui::Button("Skip tutorial")) tut.active = false;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 110);
    bool last = tut.step == (int)st.size() - 1;
    bool canNext = !s.needsAction || done;
    ImGui::BeginDisabled(!canNext);
    if (accentButton(last ? "Finish" : "Next", ImVec2(110, 0))) {
        tut.step++;
        tut.minimized = false;
        if (last) tut.active = false;
    }
    anchor("tut_next");
    ImGui::EndDisabled();
    if (!canNext && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do the step first, or skip the tutorial.");
    ImGui::End();
}

void App::winHelp() {
    if (!beginWindow(*this, W_HELP, ImVec2(160, 120), ImVec2(600, 600))) { ImGui::End(); return; }
    if (accentButton("Restart the tutorial")) { tut = Tutorial{}; tut.active = true; }
    header("Controls");
    const char* rows[][2] = {{"Space", "Pause / resume"},
                             {"1 to 5", "Game speed: pause, 1 min/s, 10 min/s, 1 h/s, 6 h/s"},
                             {"F5", "Quick save"},
                             {"M / F / J / N", "Market / Fleets / Contract Board / News Wire"},
                             {"/ or `", "Jump to the GO> command line: window codes (MKT FERROX), systems, hex ids, stations"},
                             {"F1", "This help"},
                             {"Map: drag", "Pan (left, right or middle button)"},
                             {"Map: wheel", "Zoom around the cursor"},
                             {"Map: click", "Select a system"},
                             {"Three dots", "On every title bar or tab bar: the window's options: dock left, right or bottom, tab with another window, float, pop out, reset"},
                             {"Title bar", "Double-click: dock it back where it was (or the right column). Right-click: options"},
                             {"Docked tab", "Double-click: float it again. Drag it out to float it where you drop it"},
                             {"Drag to dock", "Drag a title bar onto the arrows that appear; out of the game window onto another monitor"},
                             {"LAYOUT", "Reset the layout (also GO> RESET), dock every window, save and load named layouts"}};
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110);
        for (auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            textColored(col::Accent, "%s", r[0]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r[1]);
        }
        ImGui::EndTable();
    }
    header("How money works");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(
        "Every station has its own order book. Station populations produce some goods and consume others, so prices differ "
        "from station to station and move with supply. Instant buys take the cheapest sell orders; instant sales hit the best "
        "buy orders. Posting your own order costs a 2.5% broker fee; sales pay 2.5% tax, plus a 1% Charter levy in Charter "
        "space. Outposts charge whatever their owner sets.\n\n"
        "Ore is refined into minerals, minerals into components, components into ship modules and hulls. Ships are lost to "
        "pirates and gankers, which keeps the whole chain in demand.\n\n"
        "The law: Charter space has Wardens who destroy aggressors within a few rounds. Contract space has no police; "
        "safety there is what corporations pay for. The Frontier is unchartered: only outposts. Underwriters grade every "
        "system AAA to D from the losses and violence they see, and price insurance on it.");
    ImGui::PopTextWrapPos();
    header("What else is out there");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(
        "Fog of war (INTL): live markets and fleets only where your sensors reach: one jump around your headquarters, your "
        "fleets' own systems (further with a survey array), your outposts and your allies. Elsewhere you see the last prices "
        "you recorded, with their age. The Charter Exchange publishes its hubs' prices daily; brokers sell regional reports.\n\n"
        "Exploration (INTL, Survey order): every system hides new sites each fortnight: derelicts and relic caches to salvage, "
        "ore pockets only you know about, and short-lived drift lanes that cut across the chart. The Wisp survey frigate and "
        "the Survey Scanner module find more, faster.\n\n"
        "Diplomacy (DIPL): buy a war licence and fighting a rival becomes lawful everywhere for 14 days; make peace with or "
        "without reparations; sign non-aggression pacts and alliances (allies share sensors and defend you); hire mercenaries.\n\n"
        "Events (WIRE, Events tab; EVENTS on the chart): shortages, gluts, strikes, booms, outbreaks, Charter levies and "
        "embargoes, ion storms that close lanes, raider surges, gold rushes and Warden crackdowns. Many are announced before "
        "they start.\n\n"
        "Divisions (DIVS): put directors in the executive seats and hand them fleets. Each division (mining, trade, haulage, "
        "industry, security, intelligence) works inside the zones, radius, risk and budget you set, books its own profit and "
        "loss, and reports every 30 days. Good directors get poached; espionage steals research and smears share prices.\n\n"
        "Construction (BLD): upgrade your outposts module by module (refinery, factory, market, defence grid, silo), and found "
        "megaprojects with other corporations: jump gates between distant systems (backers share the tolls), Charter Beacons "
        "that bring the Wardens into Contract space, Trade Nexuses and survey arrays. Materials are delivered to the yard.\n\n"
        "Colonies (COLN): carry a Colony Kit to a planet in Contract space or the Frontier and settle it. Supplied colonies "
        "grow, pay taxes, run a spaceport market and hand you what the planet makes.\n\n"
        "Capital markets (FIN): futures on hub prices, bonds that you issue or buy from rivals, short selling, and an "
        "underwriting desk that insures other corporations' ships. Once you are listed, rivals may build a stake in you and "
        "demand greenmail: the Boardroom (BRD) has poison pills and white knights, and rivals use the same defences against you.");
    ImGui::PopTextWrapPos();
    header("Diagnostics");
    ImGui::Checkbox("Show the Dear ImGui demo window", &showDemo);
    ImGui::End();
}

}  // namespace nb
