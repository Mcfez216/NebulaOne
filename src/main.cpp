// NebulaOne - platform layer: window, OpenGL, Dear ImGui backends, main loop.
//
// Command line (mostly for testing):
//   --datadir <path>       use a different folder for saves and settings
//   --screenshot <file>    render, save a PNG of the window and exit
//   --scene <name>         title | game | market | allstations | fleets | industry | finance | wallet | tutorial | ship |
//                          contracts | territory | board | news | chart | all | uitest
//   --chart <size>         compact | standard | vast | galaxy | expanse (scripted scenes)
//   --frames <n>           frames to render before the screenshot (default 90)
//   --days <n>             game days to simulate before the screenshot
//   --size <w>x<h>         window size
//   --seed <n>             universe seed for scripted scenes
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "app/app.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace nb {
const unsigned char* embedded(const char* name, size_t* size);

ImTextureID createTexture(const void* rgba, int w, int h, bool repeat) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return (ImTextureID)(intptr_t)t;
}
}  // namespace nb

using namespace nb;

static std::string defaultDataDir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::current_path();
    return (base / "NebulaOne").string();
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg) : home ? fs::path(home) / ".local" / "share" : fs::current_path();
    return (base / "NebulaOne").string();
#endif
}

static void setIcon(GLFWwindow* win) {
    std::vector<GLFWimage> imgs;
    std::vector<unsigned char*> keep;
    for (const char* n : {"icon-256.png", "icon-48.png", "icon-32.png", "icon-16.png"}) {
        size_t sz = 0;
        const unsigned char* d = embedded(n, &sz);
        if (!d) continue;
        int w, h, c;
        unsigned char* px = stbi_load_from_memory(d, (int)sz, &w, &h, &c, 4);
        if (!px) continue;
        imgs.push_back({w, h, px});
        keep.push_back(px);
    }
    if (!imgs.empty()) glfwSetWindowIcon(win, (int)imgs.size(), imgs.data());
    for (auto p : keep) stbi_image_free(p);
}

// A scripted player for screenshots: a mining fleet, a trade run and some history.
static void scriptScene(App& app, const std::string& scene, int days, uint64_t seed, GalaxySize size) {
    if (scene == "title") return;
    NewGameSetup s;
    s.seed = seed;
    s.size = size;
    s.corpName = "Kestrel Orbital";
    s.ticker = "KORB";
    app.newGame(s, scene == "tutorial");
    World& w = app.world;
    if (scene == "tutorial") { app.tut.step = 4; app.openWin(W_FLEETS); return; }
    Id home = w.pc().home;
    Id homeSys = w.stations[home].system;
    Id mattock = NONE, drover = NONE;
    for (Id sid : w.pc().shipList) (w.ships[sid].def == MATTOCK ? mattock : drover) = sid;
    Command c;
    c.type = Cmd::CreateFleet; c.list = {mattock}; c.text = "Prospector";
    Id mf = app.run(c, false).id;
    c.list = {drover}; c.text = "Mule";
    Id hf = app.run(c, false).id;
    Command o;
    o.type = Cmd::SetFleetOrder; o.a = mf;
    Id beltSys = w.systems[homeSys].belts.empty() ? w.systems[homeSys].lanes[0].to : homeSys;
    o.order.kind = OrderKind::Mine; o.order.system = beltSys; o.order.belt = 0; o.order.station = home;
    app.run(o, false);
    // hire two more pilots and an executive
    int hired = 0;
    for (auto& p : w.people)
        if (p.employer == NONE && p.hub != NONE && hired < 3) { Command h; h.type = Cmd::Hire; h.a = p.id; app.run(h, false); hired++; }
    if (scene == "divisions" || scene == "divoverview") {
        // a company run by its directors: mining, trade, security and industry
        auto hireExec = [&](int seat, int skill) {
            Id cand = NONE;
            for (auto& p : w.people)
                if (p.alive && p.employer == NONE && p.hub != NONE && p.role == Role::Executive) { cand = p.id; break; }
            if (cand == NONE) cand = w.genPeople(w.hubs[0], 0, 1);
            w.people[cand].mgmt = skill;
            Command h; h.type = Cmd::Hire; h.a = cand; app.run(h, false);
            h = Command{}; h.type = Cmd::AssignExec; h.a = cand; h.b = seat; app.run(h, false);
        };
        w.pc().wallet += 120000000 * CR;
        hireExec(SEAT_MINING, 78);
        hireExec(SEAT_TRADE, 66);
        hireExec(SEAT_SECURITY, 58);
        hireExec(SEAT_INDUSTRY, 71);
        hireExec(SEAT_INTEL, 62);
        auto wing = [&](std::vector<int> defs, const char* name, int div) {
            std::vector<Id> ids;
            for (int d : defs) ids.push_back(w.spawnShip(w.player, d, home));
            Id f = w.createFleet(w.player, ids, name);
            Command a; a.type = Cmd::AssignDivision; a.a = f; a.b = div; app.run(a, false);
        };
        wing({QUARRIER, QUARRIER}, "Deep Seam", DIV_MINING);
        wing({QUARRIER}, "Second Seam", DIV_MINING);
        wing({CARRACK}, "Long Haul", DIV_TRADE);
        wing({DROVER}, "Short Haul", DIV_TRADE);
        wing({LANCER, LANCER, WARDEN}, "Watchmen", DIV_SECURITY);
        w.addItem(w.player, home, blueprint(BP_COMPONENT_FIRST).bpo, 1);
        w.addItem(w.player, home, blueprint(BP_COMPONENT_FIRST + 3).bpo, 1);
        auto on = [&](int d, DivPolicy p) { p.automate = true; Command k; k.type = Cmd::SetDivision; k.a = d; k.policy = p; app.run(k, false); };
        DivPolicy mp; mp.zones = 1; mp.maxRisk = 45; mp.radius = 6; on(DIV_MINING, mp);
        DivPolicy tp; tp.zones = 1; tp.maxRisk = 45; tp.radius = 8; tp.budget = 30000000 * CR; on(DIV_TRADE, tp);
        DivPolicy sp; sp.zones = 1; sp.maxRisk = 60; sp.radius = 6; on(DIV_SECURITY, sp);
        DivPolicy ip; ip.lines = 2; ip.budget = 20000000 * CR; on(DIV_INDUSTRY, ip);
        DivPolicy np; on(DIV_INTEL, np);
    }
    const bool capital = scene == "futures" || scene == "bonds" || scene == "shorts" || scene == "underwriting" || scene == "defence";
    if (capital) {
        // a corporation playing the capital markets
        w.pc().wallet += 400000000 * CR;
        w.pc().netWorth = w.computeNetWorth(w.player);
        Id hub = w.hubs[0], hub2 = w.hubs.size() > 1 ? w.hubs[1] : w.hubs[0];
        auto fut = [&](Id h, int it, int64_t q, bool lng, int d) {
            Command f; f.type = Cmd::OpenFuture; f.a = h; f.item = it; f.qty = q; f.flag = lng; f.b = d; app.run(f, false);
        };
        fut(hub, MINERAL_FIRST + 2, 120000, true, 14);
        fut(hub, GOOD_FIRST, 40000, false, 30);
        fut(hub2, COMPONENT_FIRST + 1, 2500, true, 30);
        Command b; b.type = Cmd::IssueBond; b.qty = 40; b.b = 90; app.run(b, false);
        b.qty = 15; b.b = 30; app.run(b, false);
        Id big = NONE;
        for (auto& oc : w.corps)
            if (oc.alive && !oc.player && (big == NONE || oc.sharePrice * oc.sharesOut > w.corps[big].sharePrice * w.corps[big].sharesOut)) big = oc.id;
        Command sh; sh.type = Cmd::OpenShort; sh.a = big; sh.qty = 15000; app.run(sh, false);
        Command u; u.type = Cmd::SetUnderwriting; u.uw.open = true; u.uw.rateMul = 1.1f; u.uw.maxRisk = 55; u.uw.capacity = 250000000 * CR;
        app.run(u, false);
    }
    if (scene == "defence") {
        // listed, and a rival is building a stake
        w.pc().founded -= 40 * MINUTES_PER_DAY;
        w.pc().netWorth = w.computeNetWorth(w.player);
        Command ipo; ipo.type = Cmd::GoPublic; ipo.f = 0.4f; app.run(ipo, false);
        for (auto& oc : w.corps)
            if (oc.alive && !oc.player && oc.arch == Arch::Pirate) { w.pc().raider = oc.id; oc.wallet += 300000000 * CR; break; }
    }
    if (scene == "construction") {
        w.pc().wallet += 400000000 * CR;
        Id farSys = NONE, from = NONE;
        for (Id f : w.systemsWithin(homeSys, 3)) {
            for (Id h : w.hubs)
                if (w.projectWhy(w.player, ProjectKind::JumpGate, f, w.stations[h].system, NONE).empty()) { farSys = w.stations[h].system; break; }
            if (farSys != NONE) { from = f; break; }
        }
        if (farSys != NONE) {
            Command g; g.type = Cmd::FoundProject; g.a = (int)ProjectKind::JumpGate; g.b = from; g.qty = farSys;
            Id gp = app.run(g, false).id;
            if (gp != NONE) {
                Project& pg = w.projects[gp];
                int k = 0;
                for (auto& [it, q] : pg.need) if (k++ % 2 == 0) w.addItem(w.player, pg.yard, it, q);
                Command cb; cb.type = Cmd::ContributeProject; cb.a = gp; cb.price = pg.cash / 3; cb.flag = true; app.run(cb, false);
            }
        }
        for (Id sy : w.systemsWithin(homeSys, 8))
            if (w.projectWhy(w.player, ProjectKind::Beacon, sy, NONE, NONE).empty()) {
                Command bc; bc.type = Cmd::FoundProject; bc.a = (int)ProjectKind::Beacon; bc.b = sy; app.run(bc, false);
                break;
            }
    }
    if (scene == "colonies") {
        w.pc().wallet += 100000000 * CR;
        Id csys = NONE;
        int cpl = -1;
        float bestHab = 0;
        for (Id sy : w.systemsWithin(homeSys, 8)) {
            if (w.systems[sy].zone != Zone::Contract) continue;
            for (auto& pl : w.planetsOf(sy))
                if (!w.colonyOn(sy, pl.index) && pl.hab * pl.size > bestHab) { bestHab = pl.hab * pl.size; csys = sy; cpl = pl.index; }
        }
        if (csys != NONE) {
            Id cs = w.spawnShip(w.player, CARRACK, home);
            Id cf = w.createFleet(w.player, {cs}, "Ark");
            w.fleets[cf].cargo[COLONY_KIT] = 1;
            w.fleets[cf].docked = NONE;
            w.fleets[cf].system = csys;
            Command st; st.type = Cmd::SettleColony; st.a = cf; st.b = cpl;
            Id col = app.run(st, false).id;
            if (col != NONE) {
                Id cst = w.colonies[col].station;
                w.addItem(w.player, cst, RATIONS, 6000);
                w.addItem(w.player, cst, MEDICAL_KITS, 1500);
                w.addItem(w.player, cst, FUEL_CELLS, 1500);
                Command sp; sp.type = Cmd::SupplyColony; sp.a = col; app.run(sp, false);
                app.selSystem = csys;
            }
        }
    }
    for (int d = 0; d < days; d++) {
        for (int m = 0; m < MINUTES_PER_DAY; m++) {
            w.tick();
            if (m % 360 == 0) {
                for (int ore = 0; ore < ORE_COUNT; ore++)
                    if (w.have(w.player, home, ore) >= 100) { Command r; r.type = Cmd::Refine; r.a = home; r.item = ore; r.qty = w.have(w.player, home, ore); app.run(r, false); }
                for (int m2 = MINERAL_FIRST; m2 < GOOD_FIRST; m2++)
                    if (w.have(w.player, home, m2) > 0) { Command r; r.type = Cmd::MarketSell; r.a = home; r.item = m2; r.qty = w.have(w.player, home, m2); r.price = 1; app.run(r, false); }
            }
        }
    }
    // a trade run in progress for the screenshot
    Id agri = NONE, colony = NONE;
    for (Id sy : w.systemsWithin(homeSys, 4))
        for (Id st : w.systems[sy].stations) {
            if (w.systems[sy].zone != Zone::Charter) continue;
            if (w.stations[st].type == StationType::Agricultural && agri == NONE) agri = st;
            if (w.stations[st].type == StationType::MiningColony && colony == NONE) colony = st;
        }
    if (agri != NONE && colony != NONE) {
        Command t; t.type = Cmd::SetFleetOrder; t.a = hf;
        t.order.kind = OrderKind::Trade; t.order.station = agri; t.order.station2 = colony; t.order.item = RATIONS;
        t.order.price = w.bestAsk(agri, RATIONS) * 103 / 100; t.order.price2 = w.bestBid(colony, RATIONS) * 95 / 100;
        t.order.qty = 8000;
        app.run(t, false);
        for (int m = 0; m < 25; m++) w.tick();
    }
    app.toasts.clear();
    app.selFleet = hf;
    app.speed = 0;
    app.selSystem = homeSys;
    if (scene == "game") { app.openWin(W_CORP); app.openWin(W_SYSTEM); }
    if (scene == "divisions") { app.openWin(W_DIVS); app.divTabReq = DIV_MINING; }
    if (scene == "divoverview") { app.openWin(W_DIVS); }
    if (scene == "market") { app.showMarket(home, FERROX, 1); }
    if (scene == "allstations") { app.showMarket(home, RATIONS, 2); }
    if (scene == "fleets") { app.openWin(W_FLEETS); app.draft.fleet = NONE; }
    if (scene == "industry") { app.openWin(W_INDUSTRY); }
    if (scene == "finance") { app.openWin(W_FINANCE); app.finTabReq = 3; }
    if (scene == "futures") { app.openWin(W_FINANCE); app.finTabReq = 5; }
    if (scene == "bonds") { app.openWin(W_FINANCE); app.finTabReq = 6; }
    if (scene == "shorts") { app.openWin(W_FINANCE); app.finTabReq = 7; }
    if (scene == "underwriting") { app.openWin(W_FINANCE); app.finTabReq = 8; }
    if (scene == "construction") { app.openWin(W_BUILD); app.buildTabReq = 1; }
    if (scene == "defence") { app.openWin(W_BOARD); }
    if (scene == "colonies") { app.openWin(W_COLONY); }
    if (scene == "wallet") { app.openWin(W_WALLET); }
    if (scene == "ship") {
        // a fitted Lancer for the Ship Information window
        Id l = w.spawnShip(w.player, LANCER, home);
        w.addItem(w.player, home, MOD_WEAPONS, 1);
        w.addItem(w.player, home, MOD_ARMOUR, 1);
        w.addItem(w.player, home, MOD_EVASION, 2);
        Command f; f.type = Cmd::FitModule; f.a = l; f.b = 0; f.item = MOD_WEAPONS; app.run(f, false);
        f.b = 1; f.item = MOD_ARMOUR; app.run(f, false);
        w.ships[l].name = "Sable Wake";
        w.ships[l].kills = 3;
        app.openShip(l);
        app.openWin(W_ASSETS, false);
    }
    if (scene == "contracts") { app.openWin(W_CONTRACTS); }
    if (scene == "territory") { app.openWin(W_TERRITORY); }
    if (scene == "board") { w.pc().wallet += 60000000 * CR; w.pc().netWorth = w.computeNetWorth(w.player); app.openWin(W_BOARD); }
    if (scene == "news") { app.openWin(W_NEWS); }
    if (scene == "chart") { app.mapMode = MapMode::Risk; app.zoom = -1; }
    if (scene == "intel") {
        // a Wisp with a Survey Scanner works the neighbourhood
        Id wisp = w.spawnShip(w.player, WISP, home);
        w.addItem(w.player, home, MOD_SCANNER, 1);
        Command f; f.type = Cmd::FitModule; f.a = wisp; f.b = 0; f.item = MOD_SCANNER; app.run(f, false);
        for (auto& p : w.people)
            if (p.employer == w.player && p.role == Role::Pilot && p.ship == NONE) { Command a; a.type = Cmd::AssignPilot; a.a = p.id; a.b = wisp; app.run(a, false); break; }
        Command cf; cf.type = Cmd::CreateFleet; cf.list = {wisp}; cf.text = "Pathfinder";
        Id pf = app.run(cf, false).id;
        Id target = homeSys;
        for (Id sy : w.systemsWithin(homeSys, 6))
            if (w.systems[sy].zone != Zone::Charter) { target = sy; break; }
        Command so; so.type = Cmd::SetFleetOrder; so.a = pf; so.order.kind = OrderKind::Survey; so.order.system = target; so.order.loop = true;
        app.run(so, false);
        for (int m = 0; m < 4 * MINUTES_PER_DAY; m++) w.tick();
        for (auto& st : w.sites)
            if (st.finder == w.player) app.selSite = st.id;
        app.selFleet = pf;
        app.mapMode = MapMode::Intel;
        app.openWin(W_INTEL);
        app.selSystem = w.fleets[pf].system;
        app.centerMap(w.fleets[pf].system);
    }
    if (scene == "events") {
        Id nextDoor = NONE;  // (not "near": windows.h defines it)
        for (Id sy : w.systemsWithin(homeSys, 3))
            if (sy != homeSys) { nextDoor = sy; break; }
        w.postEvent(EventKind::Storm, nextDoor, -1, 0, 30, 1);
        for (Id sy : w.systemsWithin(homeSys, 5))
            for (Id st : w.systems[sy].stations) {
                if (w.stations[st].type == StationType::Industrial) { w.postEvent(EventKind::Strike, st, -1, MINUTES_PER_DAY, 4, 0); goto struck; }
            }
    struck:
        w.postEvent(EventKind::Boom, w.systems[homeSys].region, -1, 0, 6, 1.8f);
        w.postEvent(EventKind::Shortage, home, MEDICAL_KITS, 0, 5, 3.2f);
        app.mapMode = MapMode::Events;
        app.openWin(W_NEWS);
        app.newsTabReq = 1;
    }
    if (scene == "diplomacy") {
        Id rival = NONE, ally = NONE;
        for (auto& oc : w.corps) {
            if (!oc.alive || oc.player) continue;
            if (oc.arch == Arch::Industrialist && rival == NONE) rival = oc.id;
            if (oc.arch == Arch::Trader && ally == NONE) ally = oc.id;
        }
        w.pc().wallet += 40000000 * CR;
        Command dw; dw.type = Cmd::DeclareWar; dw.a = rival; app.run(dw, false);
        w.corps[ally].standing[w.player] = 3.0f;
        Command np; np.type = Cmd::ProposePact; np.a = ally; np.flag = false; app.run(np, false);
        for (int m = 0; m < MINUTES_PER_DAY + 90; m++) w.tick();
        Id a1 = w.spawnShip(w.player, WARDEN, home), d1 = w.spawnShip(rival, CARRACK, home);
        Id af = w.createFleet(w.player, {a1}, "Writ of Iron"), df = w.createFleet(rival, {d1}, "Convoy");
        w.fleets[af].docked = NONE;
        w.fleets[df].docked = NONE;
        w.startBattle(homeSys, {af}, {df});
        for (int m = 0; m < 120; m++) w.tick();
        Offer of;
        of.id = w.offers.empty() ? 0 : w.offers.back().id + 1;
        of.kind = OfferKind::Peace;
        of.from = rival;
        of.to = w.player;
        of.amount = 2500000 * CR;
        of.expires = w.now + 2 * MINUTES_PER_DAY;
        of.note = w.corps[rival].name + " sues for peace and offers 2.50M cr in reparations.";
        w.offers.push_back(of);
        app.openWin(W_DIPLO);
    }
    if (scene == "all") {
        for (int k = 0; k < W_COUNT; k++) app.openWin(k, false);
        app.speed = 4;  // keep the clock running while every window draws
    }
}

int main(int argc, char** argv) {
    std::string shot, scene = "title", dataDir;
    bool forceViewports = false;
    int frames = 90, days = 0, W = 1600, H = 900;
    uint64_t seed = 20261108;
    GalaxySize chart = GalaxySize::Standard;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") shot = next();
        else if (a == "--scene") scene = next();
        else if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--days") days = std::atoi(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--datadir") dataDir = next();
        else if (a == "--chart") {
            std::string v = next();
            chart = v == "compact" ? GalaxySize::Compact : v == "vast" ? GalaxySize::Vast : v == "galaxy" ? GalaxySize::Galaxy
                    : v == "expanse" ? GalaxySize::Expanse : GalaxySize::Standard;
        }
        else if (a == "--size") { std::string v = next(); std::sscanf(v.c_str(), "%dx%d", &W, &H); }
        else if (a == "--viewports") forceViewports = true;
        else if (a == "--version") { std::printf("NebulaOne %s\n", NEBULA_VERSION); return 0; }
    }
    if (dataDir.empty()) dataDir = defaultDataDir();

    static App app;
    // settings first: detachable windows need the X11 backend on Linux (Wayland cannot place windows)
    app.dataDir = dataDir;
    app.loadSettings();
    bool viewports = (app.settings.viewports && shot.empty()) || forceViewports;
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
    if (viewports && glfwPlatformSupported(GLFW_PLATFORM_X11)) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    glfwSetErrorCallback([](int e, const char* d) { std::fprintf(stderr, "GLFW error %d: %s\n", e, d); });
    if (!glfwInit()) return 1;
    const char* glsl = "#version 330 core";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (shot.empty()) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(W, H, "NebulaOne", nullptr, nullptr);
    if (!win) {  // older drivers: fall back to a 3.0 context
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glsl = "#version 130";
        win = glfwCreateWindow(W, H, "NebulaOne", nullptr, nullptr);
    }
    if (!win) {
        std::fprintf(stderr, "NebulaOne needs OpenGL 3.0 or newer.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    setIcon(win);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    if (viewports) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    app.init(dataDir);
    if (!shot.empty()) scriptScene(app, scene, days, seed, chart);
    // high-DPI: follow the monitor's content scale unless the player set their own
    float xs = 1, ys = 1;
    glfwGetWindowContentScale(win, &xs, &ys);
    if (xs > 1.1f && app.settings.uiScale == 1.0f) { app.settings.uiScale = xs; app.applyScale(); }

    // --scene uitest: drive the real UI with injected mouse clicks
    struct Click { int frame; const char* target; };
    std::vector<Click> clicks;
    if (scene == "uitest") {
        NewGameSetup s;
        s.seed = seed;
        app.newGame(s, true);
        const char* seq[] = {"tut_next", "rail_corp", "tut_next", "rail_assets", "tut_next", "rail_market", "rail_fleets",
                             "rail_industry", "rail_finance", "rail_people", "rail_divs", "rail_corps", "rail_log", "rail_help", "rail_wallet",
                             "rail_system", "rail_ship", "rail_contracts", "rail_territory", "rail_build", "rail_colony", "rail_board", "rail_diplo", "rail_intel",
                             "rail_news", "speed"};
        int f0 = 15;
        for (const char* t : seq) { clicks.push_back({f0, t}); f0 += 12; }
        frames = std::max(frames, f0 + 20);
    }
    // --scene layout | layoutreset | layouttab | layoutsave: drive the window-layout code
    struct Timed { int frame; std::function<void()> fn; };
    std::vector<Timed> timed;
    struct RawMouse { int frame; std::string target; int button; bool down; };
    std::vector<RawMouse> raw;
    auto report = [&](const char* when) {
        std::printf("layout %s:", when);
        for (int k = 0; k < W_COUNT; k++)
            if (app.open[k])
                std::printf(" %s=%s%s", winCode(k), app.isDocked(k) ? "docked" : "floating", app.isOutside(k) ? "+outside" : "");
        std::printf("\n");
    };
    if (scene.rfind("layout", 0) == 0) {
        NewGameSetup s;
        s.seed = seed;
        app.newGame(s, false);
        for (int w : {W_MARKET, W_FLEETS, W_CORP, W_SYSTEM, W_LOG}) app.openWin(w);
        timed.push_back({6, [&] {
            app.dockSide(W_MARKET, ImGuiDir_Right);
            app.dockSide(W_FLEETS, ImGuiDir_Right);
            app.dockSide(W_LOG, ImGuiDir_Down);
            app.dockWith(W_CORP, W_SYSTEM);
        }});
        timed.push_back({30, [&] { report("docked"); }});
        if (scene == "layoutreset") {
            timed.push_back({34, [&] { app.resetLayout(false); }});
            timed.push_back({50, [&] { report("after reset"); }});
        }
        if (scene == "layouttab") {
            for (int f : {34, 36}) {
                raw.push_back({f, "tab_LOG", 0, true});
                raw.push_back({f + 1, "tab_LOG", 0, false});
            }
            timed.push_back({50, [&] { report("after double-click"); }});
        }
        if (scene == "layoutmenu") raw.push_back({34, "opt_FLT", 0, true}), raw.push_back({35, "opt_FLT", 0, false});
        if (scene == "layoutfloat") {
            raw.push_back({34, "opt_SYS", 0, true});
            raw.push_back({35, "opt_SYS", 0, false});
        }
        if (scene == "layoutsave") {
            timed.push_back({32, [&] { std::string e; std::printf("save: %d\n", (int)app.saveLayout("Test layout", &e)); }});
            timed.push_back({34, [&] { app.resetLayout(true); }});
            timed.push_back({44, [&] { report("after reset and close"); }});
            timed.push_back({46, [&] { std::string e; std::printf("load: %d\n", (int)app.loadLayout("Test layout", &e)); }});
            timed.push_back({70, [&] { report("after load"); }});
        }
        if (scene == "layoutpop") {
            timed.push_back({32, [&] { app.popOut(W_LOG, true); app.popOut(W_SYSTEM, true); }});
            timed.push_back({50, [&] { report("after pop out"); }});
            timed.push_back({52, [&] { app.popOut(W_SYSTEM, false); }});
            timed.push_back({70, [&] { report("after pop in"); }});
        }
        frames = std::max(frames, 80);
    }
    int clickRelease = -1;
    bool vsync = true;
    int frame = 0;
    auto last = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(win) && !app.quit) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED)) { ImGui_ImplGlfw_Sleep(10); continue; }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (!shot.empty()) dt = 1.0 / 60.0;
        app.update(std::min(dt, 0.25));
        if (app.settings.vsync != vsync) { vsync = app.settings.vsync; glfwSwapInterval(vsync ? 1 : 0); }

        for (auto& t : timed)
            if (t.frame == frame) t.fn();
        app.preFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        for (auto& m : raw)
            if (m.frame == frame) {
                auto it = app.anchors.find(m.target);
                if (it == app.anchors.end()) { std::printf("layout test: %s not on screen\n", m.target.c_str()); continue; }
                ImVec4 r = it->second;
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
                io.AddMouseButtonEvent(m.button, m.down);
            }
        if (!clicks.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            if (clickRelease == frame) io.AddMouseButtonEvent(0, false);
            for (auto& c : clicks)
                if (c.frame == frame) {
                    auto it = app.anchors.find(c.target);
                    if (it == app.anchors.end()) { std::printf("uitest: %s not on screen\n", c.target); continue; }
                    ImVec4 r = it->second;
                    io.AddMousePosEvent((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
                    io.AddMouseButtonEvent(0, true);
                    clickRelease = frame + 3;
                }
        }
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            // windows dragged out of the game live in their own OS windows, on any monitor
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
        frame++;
        if (!shot.empty() && frame >= frames && !clicks.empty()) {
            std::printf("uitest: tutorial step %d, windows open:", app.tut.step);
            for (int k = 0; k < W_COUNT; k++)
                if (app.open[k]) std::printf(" %s;", winTitle(k));
            std::printf("\n");
        }
        if (!shot.empty() && frame >= frames) {
            std::vector<unsigned char> px((size_t)fw * fh * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(shot.c_str(), fw, fh, 4, px.data(), fw * 4);
            std::printf("screenshot %s (%dx%d)\n", shot.c_str(), fw, fh);
            break;
        }
        glfwSwapBuffers(win);
        if (glfwWindowShouldClose(win) && app.inGame && app.settings.autosave)
            app.world.save((std::filesystem::path(dataDir) / "saves" / "Autosave.n1save").string());
    }
    app.saveSettings();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
