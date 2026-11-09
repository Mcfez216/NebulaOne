// NebulaOne - app shell: fonts, settings, saves, top bar, command rail,
// ticker, docking host, toasts and dialogs.
#include "app/app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/theme.h"
#include "app/widgets.h"
#include "imgui_internal.h"
#include "implot.h"
#include "stb_image.h"

namespace nb {

ImTextureID createTexture(const void* rgba, int w, int h, bool repeat = false);  // platform layer (main.cpp)

const double App::kSpeeds[App::kSpeedCount] = {0.0, 1.0, 10.0, 60.0, 360.0};
const char* App::kSpeedLabels[App::kSpeedCount] = {"II", "1x", "10x", "60x", "360x"};

const char* winTitle(int w) {
    static const char* t[W_COUNT] = {"Corporation", "Ledger", "Market", "Assets", "Fleets", "Ship Information", "Industry",
                                     "System", "Contract Board", "Territory", "Construction", "Colonies", "People", "Divisions", "Finance", "Corporations", "Boardroom",
                                     "Diplomacy", "Intel and Exploration", "News Wire", "Event Log", "Help"};
    return t[w];
}
const char* winCode(int w) {
    static const char* t[W_COUNT] = {"CORP", "LEDG", "MKT", "ASST", "FLT", "SHIP", "IND", "SYS", "JOBS",
                                     "TERR", "BLD", "COLN", "STAF", "DIVS", "FIN", "DIR", "BRD", "DIPL", "INTL", "WIRE", "LOG", "HELP"};
    return t[w];
}

namespace fs = std::filesystem;

static ImTextureID pngTexture(const char* name, ImVec2* size) {
    size_t n = 0;
    const unsigned char* d = embedded(name, &n);
    if (!d) return 0;
    int w, h, c;
    unsigned char* px = stbi_load_from_memory(d, (int)n, &w, &h, &c, 4);
    if (!px) return 0;
    ImTextureID t = createTexture(px, w, h);
    stbi_image_free(px);
    if (size) *size = ImVec2((float)w, (float)h);
    return t;
}

void makeBackdrop(Textures& t);  // backdrop.cpp

void App::init(const std::string& dir) {
    dataDir = dir;
    std::error_code ec;
    fs::create_directories(fs::path(dataDir) / "saves", ec);
    loadSettings();

    ImGuiIO& io = ImGui::GetIO();
    static std::string iniPath;
    iniPath = (fs::path(dataDir) / "layout.ini").string();
    io.IniFilename = iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = settings.dockShift;
    io.ConfigDockingTransparentPayload = true;  // see the dock targets through the window you are dragging
    io.ConfigViewportsNoTaskBarIcon = false;
    io.ConfigViewportsNoDecoration = true;  // detached windows keep the terminal look on any monitor

    auto font = [&](const char* name, const char* label) -> ImFont* {
        size_t n = 0;
        const unsigned char* d = embedded(name, &n);
        if (!d) return nullptr;
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        std::snprintf(cfg.Name, sizeof cfg.Name, "%s", label);
        return io.Fonts->AddFontFromMemoryTTF((void*)d, (int)n, kFontUi, &cfg);
    };
    fonts.ui = font("SairaSemiCondensed-Medium.ttf", "Saira Semi Condensed Medium");
    fonts.bold = font("SairaSemiCondensed-SemiBold.ttf", "Saira Semi Condensed SemiBold");
    fonts.mono = font("ShareTechMono-Regular.ttf", "Share Tech Mono");
    fonts.monoBold = font("ChakraPetch-SemiBold.ttf", "Chakra Petch SemiBold");
    fonts.title = fonts.monoBold;
    fonts.brand = fonts.monoBold;
    g_mono = fonts.mono;
    if (fonts.ui) io.FontDefault = fonts.ui;
    ImGui::GetStyle().FontSizeBase = kFontUi;

    for (int k = 0; k < W_COUNT; k++) lay[k].popped = (settings.popped >> k) & 1;
    tex.mark = pngTexture("nebulaone-mark-256.png", nullptr);
    tex.logo = pngTexture("nebulaone-logo-stacked-900.png", &tex.logoSize);
    makeBackdrop(tex);
    applyScale();
}

void App::applyScale() {
    applyTheme(settings.uiScale);
    ImGui::GetStyle().FontScaleMain = settings.uiScale;
}

// ------------------------------------------------------------- settings ----
void App::loadSettings() {
    std::ifstream f(fs::path(dataDir) / "settings.ini");
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "uiScale") settings.uiScale = std::clamp((float)std::atof(v.c_str()), 0.75f, 2.0f);
        if (k == "autosave") settings.autosave = v == "1";
        if (k == "mapLabels") settings.mapLabels = v == "1";
        if (k == "vsync") settings.vsync = v == "1";
        if (k == "confirmQuit") settings.confirmQuit = v == "1";
        if (k == "viewports") settings.viewports = v == "1";
        if (k == "hexGrid") settings.hexGrid = v == "1";
        if (k == "livery") settings.livery = std::clamp(std::atoi(v.c_str()), 0, LIVERY_COUNT - 1);
        if (k == "dockShift") settings.dockShift = v == "1";
        if (k == "popped") settings.popped = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
    }
}

void App::saveSettings() const {
    std::ofstream f(fs::path(dataDir) / "settings.ini");
    f << "uiScale=" << settings.uiScale << "\nautosave=" << settings.autosave << "\nmapLabels=" << settings.mapLabels
      << "\nvsync=" << settings.vsync << "\nconfirmQuit=" << settings.confirmQuit << "\nviewports=" << settings.viewports
      << "\nhexGrid=" << settings.hexGrid << "\nlivery=" << settings.livery << "\ndockShift=" << settings.dockShift
      << "\npopped=" << settings.popped << "\n";
}

// ---------------------------------------------------------------- games ----
void App::newGame(const NewGameSetup& s, bool tutorial) {
    world.generate(s);
    inGame = true;
    speed = 0;
    accum = 0;
    for (auto& o : open) o = false;
    selSystem = world.stations[world.pc().home].system;
    selStation = world.pc().home;
    mktStation = world.pc().home;
    mktItem = CINDRITE;
    selFleet = NONE;
    selShip = NONE;
    selContract = NONE;
    zoom = 0;
    tut = Tutorial{};
    tut.active = tutorial;
    currentSave = s.corpName;
    lastAutosave = 0;
    if (!tutorial) { openWin(W_CORP); }
}

static std::string safeName(const std::string& n) {
    std::string out;
    for (char c : n) out += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
    if (out.empty()) out = "Save";
    return out;
}

bool App::saveGame(const std::string& name) {
    std::string path = (fs::path(dataDir) / "saves" / (safeName(name) + ".n1save")).string();
    std::string err;
    if (!world.save(path, &err)) { toast("Save failed: " + err, false); return false; }
    currentSave = name;
    return true;
}

bool App::loadGame(const std::string& path) {
    std::string err;
    World w;
    if (!w.load(path, &err)) { toast("Could not load: " + err, false); return false; }
    world = std::move(w);
    inGame = true;
    speed = 0;
    accum = 0;
    selSystem = world.stations[world.pc().home].system;
    mktStation = world.pc().home;
    selFleet = NONE;
    selShip = NONE;
    selContract = NONE;
    zoom = 0;
    tut = Tutorial{};
    currentSave = fs::path(path).stem().string();
    lastAutosave = world.now;
    toast("Loaded " + currentSave + " (" + fmtDate(world.now) + ")");
    return true;
}

std::vector<SaveInfo> App::listSaves() const {
    std::vector<std::pair<fs::file_time_type, SaveInfo>> v;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(dataDir) / "saves", ec)) {
        if (e.path().extension() != ".n1save") continue;
        SaveInfo s;
        s.name = e.path().stem().string();
        s.path = e.path().string();
        auto t = e.last_write_time(ec);
        auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() +
                                                                                     std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char b[64];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&tt));
        s.when = b;
        v.push_back({t, s});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<SaveInfo> out;
    for (auto& p : v) out.push_back(p.second);
    return out;
}

// -------------------------------------------------------------- helpers ----
Result App::run(const Command& c, bool toastOk) {
    Command cc = c;
    cc.corp = world.player;
    Result r = world.execute(cc);
    if (!r.ok || toastOk) toast(r.msg.empty() ? (r.ok ? "Done." : "That did not work.") : r.msg, r.ok);
    return r;
}

void App::toast(const std::string& text, bool ok) {
    toasts.push_back({text, ok, time});
    while (toasts.size() > 5) toasts.pop_front();
}

void App::openWin(int w, bool focusIt) {
    open[w] = true;
    if (focusIt) focus[w] = true;
}

void App::anchor(const std::string& name) {
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    anchors[name] = ImVec4(a.x, a.y, b.x, b.y);
}

void App::openShip(Id ship) {
    selShip = ship;
    openWin(W_SHIP);
}

void App::selectSystem(Id s, bool openWindow) {
    selSystem = s;
    if (openWindow) openWin(W_SYSTEM, true);
}

void App::showMarket(Id station, int it, int tab) {
    if (station != NONE) mktStation = station;
    if (it >= 0) mktItem = it;
    mktTabReq = tab;
    openWin(W_MARKET);
}

void App::update(double dt) {
    time += dt;
    if (!inGame) return;
    double rate = kSpeeds[speed];
    if (rate <= 0) { frac = 0; return; }
    accum += dt * rate;
    int n = (int)std::floor(accum);
    n = std::min(n, 2000);
    for (int i = 0; i < n; i++) world.tick();
    accum -= n;
    if (accum > 5) accum = 0;  // do not spiral if the machine cannot keep up
    frac = accum;
    if (settings.autosave && world.now - lastAutosave >= MINUTES_PER_DAY) {
        lastAutosave = world.now;
        std::string err;
        world.save((fs::path(dataDir) / "saves" / "Autosave.n1save").string(), &err);
    }
}

// --------------------------------------------------------------- frame -----
float uiTopH() { return 46.0f * ImGui::GetStyle().FontScaleMain; }
float uiKeysH() { return 26.0f * ImGui::GetStyle().FontScaleMain; }
float uiTickerH() { return 24.0f * ImGui::GetStyle().FontScaleMain; }

void App::frame() {
    anchors.clear();
    setLivery(inGame ? world.pc().livery : settings.livery);
    // detached windows sit on the same sky as the main window
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    for (ImGuiViewport* v : pio.Viewports)
        if (v != ImGui::GetMainViewport()) drawBackdrop(v, ImVec2(pan.x * 2.0f, pan.y * 2.0f));
    drawMap();
    if (!inGame) {
        drawTitle();
        drawDialogs();
        drawToasts();
        return;
    }
    // keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) speed = speed == 0 ? 2 : 0;
        for (int k = 0; k < kSpeedCount; k++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false)) speed = k;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            if (saveGame(currentSave)) toast("Saved as " + currentSave);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) open[W_MARKET] = !open[W_MARKET];
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) open[W_FLEETS] = !open[W_FLEETS];
        if (ImGui::IsKeyPressed(ImGuiKey_J, false)) open[W_CONTRACTS] = !open[W_CONTRACTS];
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) open[W_NEWS] = !open[W_NEWS];
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) open[W_HELP] = !open[W_HELP];
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false) || ImGui::IsKeyPressed(ImGuiKey_Slash, false)) focusCmd = true;
    }
    drawTopBar();
    drawKeys();
    drawTicker();
    drawDockHost();
    processLayoutOps();
    typedef void (App::*WinFn)();
    static const WinFn fns[W_COUNT] = {&App::winCorp,    &App::winWallet,    &App::winMarket,   &App::winAssets,  &App::winFleets,
                                       &App::winShip,    &App::winIndustry,  &App::winSystem,   &App::winContracts,
                                       &App::winTerritory, &App::winBuild, &App::winColonies, &App::winPeople,  &App::winDivisions, &App::winFinance,  &App::winCorps,   &App::winBoard,
                                       &App::winDiplomacy, &App::winIntel,   &App::winNews,     &App::winLog,     &App::winHelp};
    for (int k = 0; k < W_COUNT; k++)
        if (open[k]) (this->*fns[k])();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
    drawTutorial();
    drawDialogs();
    drawToasts();
}

static void beginBar(const char* id, ImVec2 pos, ImVec2 size, ImU32 bg) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// The header: a quote-screen line. Brand, company, cash, net worth, alerts, clock and speed.
void App::drawTopBar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = uiTopH();
    beginBar("##topbar", vp->Pos, ImVec2(vp->Size.x, h), col::rgb(18, 20, 24, 225));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + vp->Size.x, p.y + h - 1), col::rgb(255, 250, 240, 30));
    // the livery stripe: bright under the brand, fading out across the bar
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + vp->Size.x * 0.6f, p.y + 2), col::Accent, withAlpha(col::Accent, 0.0f),
                                withAlpha(col::Accent, 0.0f), col::Accent);
    float ms = h - 16;
    dl->AddImage(ImTextureRef(tex.mark), ImVec2(p.x + 12, p.y + 9), ImVec2(p.x + 12 + ms, p.y + 9 + ms));
    ImGui::SetCursorPos(ImVec2(20 + ms, (h - 16) * 0.5f));
    ImGui::PushFont(fonts.brand, 15.0f);
    textColored(col::Text, "NEBULA");
    ImGui::SameLine(0, 0);
    textColored(col::Accent, "ONE");
    ImGui::PopFont();
    float x = 20 + ms + ImGui::GetItemRectSize().x * 2.0f + 30;
    auto sep = [&](float xx) { dl->AddLine(ImVec2(p.x + xx - 14, p.y + 10), ImVec2(p.x + xx - 14, p.y + h - 10), col::rgb(255, 250, 240, 26)); };
    const Corp& c = world.pc();
    sep(x);
    ImGui::SetCursorPos(ImVec2(x, 6));
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.monoBold, 16.0f);
    textColored(col::Accent, "%s", c.ticker.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 8);
    ImGui::PushFont(fonts.bold, kFontHead);
    ImGui::TextUnformatted(c.name.c_str());
    ImGui::PopFont();
    ImGui::PushFont(nullptr, kFontSmall);
    const StarSystem& hs = world.sysOf(c.home);
    textDim("%s  %s  %s", world.stations[c.home].name.c_str(), upper(zoneName(hs.zone)).c_str(), hs.hexId.c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
    x += std::max(ImGui::GetItemRectSize().x, 220.0f) + 28;
    auto stat = [&](const char* label, const std::string& v, ImU32 cc, const std::string& sub = "", ImU32 subc = col::Dim) {
        sep(x);
        ImGui::SetCursorPos(ImVec2(x, 6));
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, kFontSmall);
        textDim("%s", label);
        ImGui::PopFont();
        ImGui::PushFont(fonts.mono, 16.5f);
        textColored(cc, "%s", v.c_str());
        if (!sub.empty()) {
            ImGui::SameLine();
            ImGui::PushFont(fonts.mono, 13.0f);
            textColored(subc, "%s", sub.c_str());
            ImGui::PopFont();
        }
        ImGui::PopFont();
        ImGui::EndGroup();
        x += ImGui::GetItemRectSize().x + 28;
    };
    stat("CASH", fmtMoney(c.wallet), c.wallet < 0 ? col::Bad : col::Text);
    Money prev = c.stats.size() >= 2 ? c.stats[c.stats.size() - 2].netWorth : c.stats.empty() ? c.netWorth : c.stats.back().netWorth;
    Money d = c.netWorth - prev;
    stat("NET WORTH", fmtMoneyShort(c.netWorth), col::Text, (d >= 0 ? "+" : "") + fmtMoneyShort(d, false) + " 1D",
         d >= 0 ? col::Good : col::Bad);
    if (c.listed) stat("SHARE", fmtMoney(c.sharePrice, false), col::Accent, "BOARD " + std::to_string((int)c.board), c.board < 30 ? col::Bad : col::Dim);
    int fights = 0;
    for (auto& b : world.battles)
        if (b.active && (b.attCorp == world.player || b.defCorp == world.player)) fights++;
    int sieges = 0;
    for (Id o : world.outpostIds)
        if (!world.stations[o].dead && world.stations[o].owner == world.player && world.stations[o].besieger != NONE &&
            world.now - world.stations[o].lastSiege < 120)
            sieges++;
    int wars = 0, offersN = 0;
    for (auto& t : world.treaties)
        if (t.active && t.kind == Pact::War && (t.a == world.player || t.b == world.player) && t.live(world.now)) wars++;
    for (auto& o : world.offers) offersN += o.open && o.to == world.player && o.expires > world.now;
    int staffN = 0;
    for (auto& r : world.staffReqs) staffN += r.open && r.expires > world.now && world.people[r.person].employer == world.player;
    int raidN = world.pc().raider != NONE && world.pc().raidDeadline > 0;
    int callsN = 0;
    for (auto& f : world.futures) callsN += f.open && f.corp == world.player && f.expiry - world.now < MINUTES_PER_DAY;
    if (fights || sieges || wars || offersN || staffN || raidN || callsN) {
        sep(x);
        ImGui::SetCursorPos(ImVec2(x, h * 0.5f - 9));
        float pulse = 0.6f + 0.4f * std::sin((float)time * 6);
        ImGui::PushFont(fonts.monoBold, 15.0f);
        ImGui::BeginGroup();
        bool any = false;
        auto alert = [&](ImU32 c, const char* fmt, int n, int win) {
            if (!n) return;
            if (any) ImGui::SameLine(0, 10);
            any = true;
            textColored(c, fmt, n);
            if (ImGui::IsItemClicked()) openWin(win);
        };
        alert(withAlpha(col::Warn, pulse), "COMBAT x%d", fights, W_LOG);
        alert(withAlpha(col::Bad, pulse), "SIEGE x%d", sieges, W_TERRITORY);
        alert(col::Bad, "WAR x%d", wars, W_DIPLO);
        alert(withAlpha(col::Warn, pulse), "OFFERS %d", offersN, W_DIPLO);
        alert(withAlpha(col::Warn, pulse), "STAFF %d", staffN, W_DIVS);
        alert(withAlpha(col::Bad, pulse), "RAID", raidN, W_BOARD);
        alert(col::Warn, "EXPIRY %d", callsN, W_FINANCE);
        ImGui::EndGroup();
        ImGui::PopFont();
        x += ImGui::GetItemRectSize().x + 28;
    }
    // right side: clock, speed, menu
    float right = vp->Size.x - 12;
    float bw = 44 * settings.uiScale, mw = 52 * settings.uiScale;
    float speedW = kSpeedCount * (bw + 2);
    float clockW = 160 * settings.uiScale;
    ImGui::SetCursorPos(ImVec2(right - mw - speedW - clockW - 10, 6));
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("GAME TIME");
    ImGui::PopFont();
    ImGui::PushFont(fonts.mono, 16.5f);
    ImGui::TextUnformatted(fmtDate(world.now).c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::SetCursorPos(ImVec2(right - mw - speedW, (h - 24) * 0.5f));
    ImGui::BeginGroup();
    for (int k = 0; k < kSpeedCount; k++) {
        if (k) ImGui::SameLine(0, 2);
        bool on = speed == k;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, k == 0 ? withAlpha(col::Warn, 0.40f) : col::Accent);
            ImGui::PushStyleColor(ImGuiCol_Text, k == 0 ? col::Text : col::OnAccent);
        }
        ImGui::PushFont(fonts.monoBold, 13.5f);
        if (ImGui::Button(kSpeedLabels[k], ImVec2(bw, 24))) speed = k;
        ImGui::PopFont();
        if (on) ImGui::PopStyleColor(2);
        static const char* tips[] = {"Paused (Space)", "1 game minute per second (2)", "10 game minutes per second (3)",
                                     "1 game hour per second (4)", "6 game hours per second (5)"};
        ImGui::SetItemTooltip("%s", tips[k]);
    }
    ImGui::EndGroup();
    anchor("speed");
    ImGui::SameLine(0, 8);
    ImGui::PushFont(fonts.monoBold, 13.5f);
    if (ImGui::Button("MENU", ImVec2(mw, 24))) ImGui::OpenPopup("gamemenu");
    ImGui::PopFont();
    if (ImGui::BeginPopup("gamemenu")) {
        if (ImGui::MenuItem("Save", "F5")) { if (saveGame(currentSave)) toast("Saved as " + currentSave); }
        if (ImGui::MenuItem("Save as...")) showSave = true;
        if (ImGui::MenuItem("Load...")) showLoad = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) showSettings = true;
        if (ImGui::MenuItem("Help and tutorial", "F1")) openWin(W_HELP);
        if (ImGui::MenuItem("Restart tutorial")) { tut = Tutorial{}; tut.active = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit to title")) {
            if (settings.autosave) world.save((fs::path(dataDir) / "saves" / "Autosave.n1save").string());
            inGame = false;
        }
        if (ImGui::MenuItem("Quit to desktop")) showQuitConfirm = true;
        ImGui::EndPopup();
    }
    ImGui::End();
}

// The function-key strip: every window by its mnemonic, and a command line.
void App::drawKeys() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = uiKeysH();
    beginBar("##keys", ImVec2(vp->Pos.x, vp->Pos.y + uiTopH()), ImVec2(vp->Size.x, h), col::rgb(12, 14, 17, 205));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + vp->Size.x, p.y + h - 1), col::rgb(255, 250, 240, 22));
    struct K { int win; const char* anchorName; };
    static const K keys[] = {{W_CORP, "rail_corp"},         {W_WALLET, "rail_wallet"},   {W_MARKET, "rail_market"},
                             {W_ASSETS, "rail_assets"},     {W_FLEETS, "rail_fleets"},   {W_SHIP, "rail_ship"},
                             {W_INDUSTRY, "rail_industry"}, {W_SYSTEM, "rail_system"},   {W_CONTRACTS, "rail_contracts"},
                             {W_TERRITORY, "rail_territory"}, {W_BUILD, "rail_build"}, {W_COLONY, "rail_colony"}, {W_PEOPLE, "rail_people"}, {W_DIVS, "rail_divs"}, {W_FINANCE, "rail_finance"},
                             {W_CORPS, "rail_corps"},       {W_BOARD, "rail_board"},     {W_DIPLO, "rail_diplo"},
                             {W_INTEL, "rail_intel"},       {W_NEWS, "rail_news"},
                             {W_LOG, "rail_log"},           {W_HELP, "rail_help"}};
    ImGui::SetCursorPos(ImVec2(8, 2));
    ImGui::PushFont(fonts.monoBold, 13.0f);
    float bh = h - 4;
    // fit the strip to the window: tighter keys, a shorter command line, then a MORE key for the rest
    const int N = (int)(sizeof keys / sizeof *keys);
    float cw = 330 * settings.uiScale;
    auto stripW = [&](int n, float pad, bool more) {
        float sw = 0;
        for (int i = 0; i < n; i++) sw += ImGui::CalcTextSize(winCode(keys[i].win)).x + pad + 2;
        if (more) sw += ImGui::CalcTextSize("MORE").x + pad + 2;
        return sw + 10 + ImGui::CalcTextSize("LAYOUT").x + pad;
    };
    float pad = 18;
    float avail = vp->Size.x - cw - 34;
    if (stripW(N, pad, false) > avail) pad = 10;
    if (stripW(N, pad, false) > avail) {
        cw = std::max(190 * settings.uiScale, vp->Size.x - 34 - stripW(N, pad, false));
        avail = vp->Size.x - cw - 34;
    }
    int shown = N;
    if (stripW(N, pad, false) > avail)
        while (shown > 1 && stripW(shown, pad, true) > avail) shown--;
    for (int i = 0; i < shown; i++) {
        const K& k = keys[i];
        if (i) ImGui::SameLine(0, 2);
        bool on = open[k.win];
        ImVec2 c = ImGui::GetCursorScreenPos();
        float tw = ImGui::CalcTextSize(winCode(k.win)).x + pad;
        ImGui::PushID(k.win);
        if (ImGui::InvisibleButton("k", ImVec2(tw, bh))) {
            open[k.win] = !open[k.win];
            if (open[k.win]) focus[k.win] = true;
        }
        bool hov = ImGui::IsItemHovered();
        if (on || hov) dl->AddRectFilled(c, ImVec2(c.x + tw, c.y + bh), on ? withAlpha(col::Accent, 0.16f) : col::rgb(255, 255, 255, 14), 2);
        if (on) dl->AddRectFilled(ImVec2(c.x + 3, c.y + bh - 2), ImVec2(c.x + tw - 3, c.y + bh), col::Accent);
        ImVec2 ts = ImGui::CalcTextSize(winCode(k.win));
        dl->AddText(ImVec2(c.x + (tw - ts.x) * 0.5f, c.y + (bh - ts.y) * 0.5f - 1), on ? col::Accent : hov ? col::Text : col::Dim, winCode(k.win));
        if (hov) ImGui::SetTooltip("%s", winTitle(k.win));
        anchor(k.anchorName);
        ImGui::PopID();
    }
    if (shown < N) {  // the keys that did not fit
        ImGui::SameLine(0, 2);
        ImVec2 c = ImGui::GetCursorScreenPos();
        float tw = ImGui::CalcTextSize("MORE").x + pad;
        bool anyOpen = false;
        for (int i = shown; i < N; i++) anyOpen |= open[keys[i].win];
        if (ImGui::InvisibleButton("morekeys", ImVec2(tw, bh))) ImGui::OpenPopup("morekeys");
        bool hov = ImGui::IsItemHovered();
        if (anyOpen || hov)
            dl->AddRectFilled(c, ImVec2(c.x + tw, c.y + bh), anyOpen ? withAlpha(col::Accent, 0.16f) : col::rgb(255, 255, 255, 14), 2);
        ImVec2 ts = ImGui::CalcTextSize("MORE");
        dl->AddText(ImVec2(c.x + (tw - ts.x) * 0.5f, c.y + (bh - ts.y) * 0.5f - 1), anyOpen ? col::Accent : hov ? col::Text : col::Dim, "MORE");
        ImGui::PushFont(fonts.ui, kFontUi);
        if (ImGui::BeginPopup("morekeys")) {
            for (int i = shown; i < N; i++) {
                const K& k = keys[i];
                if (ImGui::MenuItem(winTitle(k.win), winCode(k.win), open[k.win])) {
                    open[k.win] = !open[k.win];
                    if (open[k.win]) focus[k.win] = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopFont();
    }
    // LAYOUT: reset, dock everything, saved layouts
    {
        ImGui::SameLine(0, 10);
        ImVec2 c = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(c.x - 6, c.y + 4), ImVec2(c.x - 6, c.y + bh - 4), col::rgb(255, 250, 240, 30));
        const char* lbl = "LAYOUT";
        float tw = ImGui::CalcTextSize(lbl).x + pad;
        if (ImGui::InvisibleButton("layoutkey", ImVec2(tw, bh))) ImGui::OpenPopup("layoutmenu");
        bool hov = ImGui::IsItemHovered(), on = ImGui::IsPopupOpen("layoutmenu");
        if (on || hov) dl->AddRectFilled(c, ImVec2(c.x + tw, c.y + bh), on ? withAlpha(col::Accent, 0.16f) : col::rgb(255, 255, 255, 14), 2);
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(c.x + (tw - ts.x) * 0.5f, c.y + (bh - ts.y) * 0.5f - 1), on ? col::Accent : hov ? col::Text : col::Dim, lbl);
        if (hov && !on) ImGui::SetTooltip("Reset the window layout, dock everything, saved layouts");
        anchor("rail_layout");
        ImGui::PushFont(fonts.ui, kFontUi);
        if (ImGui::BeginPopup("layoutmenu")) {
            layoutMenu();
            ImGui::EndPopup();
        }
        ImGui::PopFont();
    }
    ImGui::PopFont();
    // the command line: GO> MKT FERROX, a system name, a hex id, a station...
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16, vp->Size.x - cw - 10));
    ImGui::SetCursorPosY(2);
    ImGui::PushFont(fonts.monoBold, 13.5f);
    textColored(col::Accent, "GO>");
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(cw - 40);
    if (focusCmd) { ImGui::SetKeyboardFocusHere(); focusCmd = false; }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::rgb(255, 255, 255, 12));
    bool enter = ImGui::InputTextWithHint("##cmd", "MKT FERROX, a system, hex, station...  ( / )", cmdLine, sizeof cmdLine,
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsUppercase);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    anchor("cmdline");
    if (enter) { runCommandLine(); cmdLine[0] = 0; }
    ImGui::End();
}

void App::runCommandLine() {
    std::string line = upper(cmdLine);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    if (line.empty()) return;
    std::string head = line, rest;
    auto sp = line.find(' ');
    if (sp != std::string::npos) { head = line.substr(0, sp); rest = line.substr(sp + 1); }
    auto findItem = [&](const std::string& q) {
        for (int i = 0; i < ITEM_COUNT; i++)
            if (upper(item(i).name).rfind(q, 0) == 0) return i;
        for (int i = 0; i < ITEM_COUNT; i++)
            if (upper(item(i).name).find(q) != std::string::npos) return i;
        return -1;
    };
    for (int k = 0; k < W_COUNT; k++)
        if (head == winCode(k)) {
            openWin(k);
            if (k == W_MARKET && !rest.empty()) {
                int it = findItem(rest);
                if (it >= 0) mktItem = it;
            }
            return;
        }
    if (head == "RESET" || (head == "LAYOUT" && (rest.empty() || rest == "RESET"))) { resetLayout(false); return; }
    if (head == "LAYOUT") {
        for (auto& n : listLayouts())
            if (upper(n) == rest) {
                std::string err;
                if (!loadLayout(n, &err)) toast(err, false);
                return;
            }
        toast("No saved layout called " + rest + " (LAYOUT key to save one)", false);
        return;
    }
    if (head == "HOME") { selectSystem(world.stations[world.pc().home].system); centerMap(selSystem); return; }
    if (head == "SAVE") { if (saveGame(currentSave)) toast("Saved as " + currentSave); return; }
    if (head == "PAUSE") { speed = 0; return; }
    // a hex designation, then a system, a station, an item
    for (auto& s : world.systems)
        if (upper(s.hexId) == line || upper(s.name) == line) { selectSystem(s.id); centerMap(s.id); return; }
    for (auto& st : world.stations)
        if (!st.dead && upper(st.name).find(line) != std::string::npos) { showMarket(st.id, -1, 0); centerMap(st.system); return; }
    int it = findItem(line);
    if (it >= 0) { showMarket(NONE, it, 0); return; }
    for (auto& s : world.systems)
        if (upper(s.name).rfind(line, 0) == 0) { selectSystem(s.id); centerMap(s.id); return; }
    toast("GO> " + line + ": not a window code, system, hex, station or item.", false);
}

// The tape: prices at your home market scrolling past, then the latest headlines.
void App::drawTicker() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = uiTickerH();
    beginBar("##ticker", ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h), ImVec2(vp->Size.x, h), col::rgb(12, 14, 17, 215));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    dl->AddLine(p, ImVec2(p.x + vp->Size.x, p.y), col::rgb(255, 250, 240, 22));
    struct Seg { std::string text; ImU32 c; };
    std::vector<Seg> segs;
    Id hub = world.pc().home;
    static const int tape[] = {FERROX, SILVANE, CORIEL, VANTIUM, LUCENITE, ZYRIUM, RATIONS, FUEL_CELLS, MEDICAL_KITS, ELECTRONICS,
                               LUXURY_GOODS, HULL_PLATING, THRUSTERS, POWER_CORE, CINDRITE, PALLASITE};
    for (int it : tape) {
        Money px = world.refPrice(hub, it);
        double chg = 0;
        if (const Book* bk = world.stations[hub].findBook(it)) {
            // day on day: today's close (or the last) against the one before
            size_t n = bk->hist.size();
            double last = bk->tradedToday ? bk->today.close : n ? bk->hist[n - 1].close : 0;
            double prev = bk->tradedToday ? (n ? bk->hist[n - 1].close : 0) : n > 1 ? bk->hist[n - 2].close : 0;
            if (last > 0 && prev > 0) { chg = (last / prev - 1.0) * 100.0; px = credits(last); }
        }
        char b[96];
        std::string nm = upper(item(it).name);
        if (nm.size() > 10) nm = nm.substr(0, 10);
        std::snprintf(b, sizeof b, "%s %s", nm.c_str(), fmtMoney(px, false).c_str());
        segs.push_back({b, col::Text});
        std::snprintf(b, sizeof b, " %s%.1f%%", chg > 0.05 ? "+" : chg < -0.05 ? "" : " ", chg);
        segs.push_back({b, chg > 0.05 ? col::Good : chg < -0.05 ? col::Bad : col::Dim});
        segs.push_back({"   ", col::Dim});
    }
    int shown = 0;
    for (auto it = world.log.rbegin(); it != world.log.rend() && shown < 4; ++it) {
        if (!it->mine && it->chan != Chan::News) continue;
        ImU32 c = it->sev == Sev::Bad ? col::Bad : it->sev == Sev::Warn ? col::Warn : it->sev == Sev::Good ? col::Good : col::Text;
        segs.push_back({"//  " + fmtDate(it->t).substr(5) + "  ", col::Dim});
        segs.push_back({it->text + "   ", c});
        shown++;
    }
    ImFont* f = fonts.mono;
    float fs = 13.5f * settings.uiScale;
    float total = 0;
    for (auto& s : segs) total += f->CalcTextSizeA(fs, FLT_MAX, 0, s.text.c_str()).x;
    total = std::max(total + 60.0f, vp->Size.x * 0.5f);
    float label = 96 * settings.uiScale;
    float off = std::fmod((float)time * 42.0f, total);
    dl->PushClipRect(ImVec2(p.x + label, p.y), ImVec2(p.x + vp->Size.x - 104 * settings.uiScale, p.y + h), true);
    for (int rep = 0; rep < 3; rep++) {
        float x = p.x + label + 8 - off + rep * total;
        if (x > p.x + vp->Size.x) break;
        for (auto& s : segs) {
            float w = f->CalcTextSizeA(fs, FLT_MAX, 0, s.text.c_str()).x;
            if (x + w > p.x && x < p.x + vp->Size.x) dl->AddText(f, fs, ImVec2(x, p.y + (h - fs) * 0.5f), s.c, s.text.c_str());
            x += w;
        }
    }
    dl->PopClipRect();
    dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(p.x + label, p.y + h), withAlpha(col::Accent, 0.85f));
    char lb[48];
    std::snprintf(lb, sizeof lb, "%s", upper(world.systems[world.stations[hub].system].name).substr(0, 9).c_str());
    dl->AddText(fonts.monoBold, fs, ImVec2(p.x + 8, p.y + (h - fs) * 0.5f - 1), col::OnAccent, lb);
    ImGui::SetCursorPos(ImVec2(vp->Size.x - 100 * settings.uiScale, (h - ImGui::GetFrameHeight()) * 0.5f + 1));
    ImGui::PushFont(fonts.monoBold, 12.5f);
    if (ImGui::SmallButton("WIRE >")) openWin(W_NEWS);
    ImGui::SameLine();
    if (ImGui::SmallButton("LOG")) openWin(W_LOG);
    ImGui::PopFont();
    ImGui::End();
}

void App::drawDockHost() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float top = uiTopH() + uiKeysH();
    ImVec2 pos(vp->Pos.x, vp->Pos.y + top);
    ImVec2 size(vp->Size.x, vp->Size.y - top - uiTickerH());
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##dockhost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    dockId = ImGui::GetID("NebulaDock");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void App::drawToasts() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    float y = vp->Pos.y + (inGame ? uiTopH() + uiKeysH() : 0) + 12;
    float wmax = 420 * settings.uiScale;
    while (!toasts.empty() && time - toasts.front().t0 > 5.0) toasts.pop_front();
    for (auto& t : toasts) {
        float age = (float)(time - t.t0);
        float a = std::clamp(std::min(age * 6.0f, (5.0f - age) * 2.0f), 0.0f, 1.0f);
        ImVec2 ts = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, wmax - 30);
        float w = std::min(wmax, ts.x + 34), h = ts.y + 16;
        ImVec2 p(vp->Pos.x + vp->Size.x - w - 16, y);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), withAlpha(col::PanelHi, a * 0.94f), 3);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), withAlpha(col::Border, a), 3);
        dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + h), withAlpha(t.ok ? col::Accent : col::Bad, a), 3, ImDrawFlags_RoundCornersLeft);
        dl->AddText(nullptr, 0, ImVec2(p.x + 14, p.y + 8), withAlpha(col::Text, a), t.text.c_str(), nullptr, wmax - 30);
        y += h + 6;
    }
}

// --------------------------------------------------------------- dialogs ---
void App::drawDialogs() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp->GetCenter();
    auto modal = [&](const char* id, bool& flag) {
        if (flag) { ImGui::OpenPopup(id); flag = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        return ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    };
    static char saveName[64] = "";
    if (showSave) std::snprintf(saveName, sizeof saveName, "%s", currentSave.c_str());
    if (modal("Save game", showSave)) {
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Name", saveName, sizeof saveName);
        auto saves = listSaves();
        if (!saves.empty()) {
            textDim("Existing saves (click to overwrite):");
            ImGui::BeginChild("saves", ImVec2(380, 160), ImGuiChildFlags_Borders);
            for (auto& s : saves)
                if (ImGui::Selectable((s.name + "##" + s.path).c_str())) std::snprintf(saveName, sizeof saveName, "%s", s.name.c_str());
            ImGui::EndChild();
        }
        if (accentButton("Save", ImVec2(120, 0)) && saveName[0]) {
            if (saveGame(saveName)) toast(std::string("Saved as ") + saveName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (modal("Load game", showLoad)) {
        auto saves = listSaves();
        if (saves.empty()) textDim("No saved games yet.");
        if (ImGui::BeginTable("loadlist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(520, 260))) {
            ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70);
            for (auto& s : saves) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn();
                textDim("%s", s.when.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(s.path.c_str());
                if (ImGui::SmallButton("Load")) {
                    loadGame(s.path);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (modal("Settings", showSettings)) {
        static float scale = 1.0f;
        if (ImGui::IsWindowAppearing()) scale = settings.uiScale;
        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat("Interface scale", &scale, 0.8f, 1.8f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) { settings.uiScale = scale; applyScale(); }
        ImGui::Checkbox("Autosave every game day", &settings.autosave);
        ImGui::Checkbox("Show system names on the map", &settings.mapLabels);
        ImGui::Checkbox("Draw the hex grid on the chart", &settings.hexGrid);
        ImGui::Checkbox("Detachable windows (drag onto other monitors)", &settings.viewports);
        helpMark("Lets any window leave the game window and sit on another monitor. Takes effect the next time you start NebulaOne.");
        ImGui::Checkbox("Vertical sync", &settings.vsync);
        ImGui::Checkbox("Confirm before quitting", &settings.confirmQuit);
        ImGui::Spacing();
        if (ImGui::Button("Put a shortcut on the desktop")) {
            std::string where, err;
            if (createDesktopShortcut(&where, &err)) toast("Shortcut created: " + where, true);
            else toast("Could not create the shortcut: " + err, false);
        }
        helpMark("The installer already does this. Use it if you run NebulaOne from the portable zip or the Linux tarball.");
        textDim("Data folder: %s", dataDir.c_str());
        ImGui::Spacing();
        if (accentButton("Done", ImVec2(120, 0))) { saveSettings(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (modal("Quit NebulaOne?", showQuitConfirm)) {
        ImGui::TextUnformatted(inGame ? "Your game is autosaved when you quit." : "Leave NebulaOne?");
        if (accentButton("Quit", ImVec2(120, 0))) {
            if (inGame && settings.autosave) world.save((fs::path(dataDir) / "saves" / "Autosave.n1save").string());
            quit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stay", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // new game dialog
    static char nm[48] = "Kestrel Orbital", tk[8] = "KORB";
    static int seed = 0;
    static bool withTutorial = true;
    static int sizeIdx = 1;
    if (showNewGame) seed = (int)(std::chrono::steady_clock::now().time_since_epoch().count() % 900000) + 100000;
    if (modal("New corporation", showNewGame)) {
        textDim("Name your company. Everything else you build.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(280);
        if (ImGui::InputText("Corporation name", nm, sizeof nm)) {
            // suggest a ticker: initials, topped up from the first word
            std::string t, first;
            bool start = true;
            for (const char* q = nm; *q; q++) {
                bool alpha = std::isalpha((unsigned char)*q) != 0;
                if (alpha && start) t += (char)std::toupper((unsigned char)*q);
                if (alpha && t.size() <= 1) first += (char)std::toupper((unsigned char)*q);
                start = !alpha;
            }
            for (size_t k = 1; t.size() < 4 && k < first.size(); k++) t += first[k];
            std::snprintf(tk, sizeof tk, "%s", t.substr(0, 5).c_str());
        }
        ImGui::SetNextItemWidth(90);
        ImGui::InputText("Ticker", tk, sizeof tk, ImGuiInputTextFlags_CharsUppercase);
        ImGui::SetNextItemWidth(140);
        ImGui::InputInt("Universe seed", &seed);
        helpMark("The seed shapes the map, its names and its economy. Share it to play the same universe.");
        static const char* sizes[GALAXY_SIZE_COUNT] = {"Compact: 80 systems, 12 rivals", "Standard: 400 systems, 40 rivals",
                                                       "Vast: 2,000 systems, 120 rivals", "Galaxy: 8,000 systems, 300 rivals",
                                                       "Expanse: 20,000 systems, 600 rivals (experimental)"};
        ImGui::SetNextItemWidth(320);
        ImGui::Combo("Chart size", &sizeIdx, sizes, GALAXY_SIZE_COUNT);
        helpMark("The whole chart is generated from the seed. Compact suits the tutorial; Galaxy is the size of the largest "
                 "space MMOs; Expanse is bigger still and needs a fast machine at top speed.");
        static bool fogOn = true;
        ImGui::Checkbox("Fog of war", &fogOn);
        helpMark("On: you see live markets and fleets only where your fleets, headquarters, outposts and allies are. Elsewhere "
                 "you trade on the last prices you saw, or buy intel. Off: the whole chart is an open book.");
        ImGui::AlignTextToFramePadding();
        textDim("Livery");
        ImGui::SameLine(110);
        if (liveryPicker("##livery", settings.livery)) setLivery(settings.livery);
        ImGui::Checkbox("Start with the tutorial", &withTutorial);
        ImGui::Spacing();
        if (accentButton("Found the corporation", ImVec2(220, 0)) && nm[0]) {
            NewGameSetup s;
            s.fog = fogOn;
            s.livery = settings.livery;
            saveSettings();
            s.seed = (uint64_t)std::max(1, seed);
            s.corpName = nm;
            s.ticker = tk[0] ? tk : "CORP";
            s.size = (GalaxySize)std::clamp(sizeIdx, 0, GALAXY_SIZE_COUNT - 1);
            newGame(s, withTutorial);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace nb
