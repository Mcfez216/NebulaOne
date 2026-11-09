// NebulaOne - the game client: UI state and every window.
// The UI reads the World freely but changes it only through App::run(Command).
#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "core/world.h"
#include "imgui.h"

namespace nb {

enum Win : int {
    W_CORP = 0, W_WALLET, W_MARKET, W_ASSETS, W_FLEETS, W_SHIP, W_INDUSTRY, W_SYSTEM, W_CONTRACTS, W_TERRITORY, W_BUILD, W_COLONY, W_PEOPLE,
    W_DIVS, W_FINANCE, W_CORPS, W_BOARD, W_DIPLO, W_INTEL, W_NEWS, W_LOG, W_HELP, W_COUNT
};
const char* winTitle(int w);
const char* winCode(int w);  // the terminal mnemonic: MKT, FLT, ...

enum class MapMode : int { Zones = 0, Risk, Sovereignty, Regions, Intel, Events };

struct Toast {
    std::string text;
    bool ok = true;
    double t0 = 0;
};

struct Fonts {
    ImFont* ui = nullptr;        // Saira Semi Condensed Medium: interface text
    ImFont* bold = nullptr;      // Saira Semi Condensed SemiBold
    ImFont* mono = nullptr;      // Share Tech Mono: figures and tables
    ImFont* monoBold = nullptr;  // Chakra Petch SemiBold: keys, labels, codes
    ImFont* title = nullptr;     // Chakra Petch SemiBold: window titles and big names
    ImFont* brand = nullptr;
};

struct Textures {
    ImTextureID mark = 0, logo = 0, nebula = 0, starsFar = 0, starsNear = 0;
    ImVec2 logoSize{1, 1};
};

struct Settings {
    float uiScale = 1.0f;
    bool autosave = true;
    bool mapLabels = true;
    bool vsync = true;
    bool confirmQuit = true;
    bool viewports = true;  // windows can be dragged out of the game onto other monitors
    bool hexGrid = true;
    int livery = 0;  // the last livery chosen: colours the title screen and new games
    bool dockShift = false;  // only dock while holding Shift (stops accidental docking)
    uint32_t popped = 0;     // windows kept in their own OS window, one bit each
};

struct Tutorial {
    bool active = false;
    int step = 0;
    bool marketAllSeen = false;
    bool minimized = false;
};

struct SaveInfo {
    std::string name, path, when;
};

// Where a window lives: its default place, and layout actions waiting for the next frame.
struct WinLayout {
    ImVec2 defPos{0, 0}, defSize{0, 0};  // unscaled, from the window's own code
    bool popped = false;                 // kept in its own OS window, even over the game window
    int place = 0;                       // frames left of forcing the default place (reset)
    bool atMouse = false;                // place it under the mouse instead (after a double-click undock)
    unsigned lastDock = 0;               // the dock node it was last in, to dock it back
    int pendingSide = -1;                // docking to a side column: remember the node it lands in
    int pendingFrames = 0;
};
// A queued dock or undock: some take two frames (undock first, then dock).
struct LayoutOp {
    enum Kind { Side, With, Node, Undock, PopOut, PopIn } kind = Side;
    int w = 0;
    int dir = 1;          // ImGuiDir for Side
    int target = -1;      // window for With
    unsigned node = 0;    // dock node for Node
    int age = 0;          // frames waiting
};

// Fleet order editor state (kept between frames while a fleet is selected)
struct OrderDraft {
    Id fleet = NONE;
    FleetOrder o;
    int kindIdx = 0;
    char qtyBuf[32] = "";
};

class App {
public:
    World world;
    bool inGame = false;
    int speed = 0;           // index into kSpeeds
    double accum = 0;        // pending game minutes
    double frac = 0;         // 0..1 progress into the next tick, for smooth motion
    bool open[W_COUNT] = {};
    bool focus[W_COUNT] = {};
    Id selSystem = NONE, selStation = NONE, selFleet = NONE, selCorp = NONE, selShip = NONE, selContract = NONE, selSite = NONE;
    int newsTabReq = -1;
    MapMode mapMode = MapMode::Zones;
    char cmdLine[96] = "";
    bool focusCmd = false;
    Id mktStation = NONE;
    int mktItem = CINDRITE;
    int mktTabReq = -1;
    int finTabReq = -1;
    int buildTabReq = -1;  // Construction window: 0 outposts, 1 megaprojects, 2 found
    int divTabReq = -1;  // Divisions window: open on this division's tab
    OrderDraft draft;
    ImVec2 pan{0, 0};
    float zoom = 0;  // 0 = fit on next frame
    std::deque<Toast> toasts;
    Tutorial tut;
    Settings settings;
    Fonts fonts;
    Textures tex;
    std::map<std::string, ImVec4> anchors;  // screen rects of things the tutorial points at
    std::string dataDir;
    std::string currentSave = "Autosave";
    Tick lastAutosave = 0;
    bool quit = false;
    bool showSettings = false, showSave = false, showLoad = false, showNewGame = false, showQuitConfirm = false;
    bool showDemo = false;
    double time = 0;  // real seconds since start

    // window layout (layout.cpp)
    WinLayout lay[W_COUNT];
    std::vector<LayoutOp> layoutOps;
    unsigned dockId = 0;            // the main dockspace
    unsigned sideNode[4] = {};      // the side columns made by "Dock left/right/top/bottom", by ImGuiDir
    bool resetQueued = false, resetCloseAll = false;
    std::string pendingLayout;      // a saved layout to load before the next frame
    bool pendingLayoutHasOpen = false;
    bool pendingOpen[W_COUNT] = {};
    void preFrame();                // between frames: layout resets and loads
    void resetLayout(bool closeAll = false);
    void dockSide(int w, int dir);
    void dockWith(int w, int target);
    void undock(int w, bool atMouse = false);
    void popOut(int w, bool out);
    void processLayoutOps();
    void windowMenu(int w);         // the per-window menu: dock, undock, pop out, reset
    void layoutMenu();              // the LAYOUT key's menu
    bool saveLayout(const std::string& name, std::string* err);
    bool loadLayout(const std::string& name, std::string* err);
    std::vector<std::string> listLayouts() const;
    void deleteLayout(const std::string& name);
    bool isDocked(int w) const;
    bool isOutside(int w) const;    // in its own OS window

    static constexpr int kSpeedCount = 5;
    static const double kSpeeds[kSpeedCount];
    static const char* kSpeedLabels[kSpeedCount];

    void init(const std::string& dataDir);
    void update(double dt);  // advance the simulation
    void frame();            // build the whole UI for this frame
    Result run(const Command& c, bool toastOk = true);
    void toast(const std::string& text, bool ok = true);
    void openWin(int w, bool focusIt = true);
    void newGame(const NewGameSetup& s, bool tutorial);
    bool saveGame(const std::string& name);
    bool loadGame(const std::string& path);
    std::vector<SaveInfo> listSaves() const;
    void loadSettings();
    void saveSettings() const;
    void applyScale();
    void anchor(const std::string& name);  // remember the last item's rect
    void selectSystem(Id s, bool openWindow = true);
    void showMarket(Id station, int item, int tab = -1);
    void centerMap(Id system);
    std::string stationLabel(Id st) const;

    // frame pieces
    void drawTitle();
    void drawTopBar();
    void drawKeys();
    void drawTicker();
    void runCommandLine();
    void openShip(Id ship);
    void drawDockHost();
    void drawMap();
    void drawBackdrop(ImGuiViewport* vp, ImVec2 drift);
    void drawToasts();
    void drawTutorial();
    void drawDialogs();

    // windows
    void winShip();
    void winContracts();
    void winTerritory();
    void winBoard();
    void winNews();
    void winDiplomacy();
    void winIntel();
    void winCorp();
    void winWallet();
    void winMarket();
    void winAssets();
    void winFleets();
    void winIndustry();
    void winSystem();
    void winPeople();
    void winDivisions();
    void winBuild();
    void winColonies();
    void winFinance();
    void winCorps();
    void winLog();
    void winHelp();

private:
    void fleetOrderEditor(Fleet& f);
    void formFleetPanel();
};

}  // namespace nb
