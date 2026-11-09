// NebulaOne - window layout. Every window opens at a sensible default place;
// from there it can be docked into side columns or tabbed with another window,
// undocked, popped out into its own OS window (another monitor), reset, or
// saved with the rest of the layout under a name.
//
// Shortcuts: double-click a floating window's title bar to dock it back where it
// was (or the right-hand column); double-click a docked tab to float it again;
// right-click either, or press the window's ⋮ button, for every option.
// LAYOUT on the function strip resets the whole layout.
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/app.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "imgui_internal.h"

namespace nb {
namespace fs = std::filesystem;

namespace {

std::string winName(int w) { return std::string("###") + winTitle(w); }
ImGuiWindow* findWin(int w) { return ImGui::FindWindowByName(winName(w).c_str()); }

ImGuiDockNode* liveNode(unsigned id) {
    if (!id) return nullptr;
    return ImGui::DockContextFindNodeByID(GImGui, id);
}

// A side column we made earlier, if it is still there and still a column.
ImGuiDockNode* sideColumn(App& a, int dir) {
    ImGuiDockNode* n = liveNode(a.sideNode[dir]);
    if (!n || !n->IsLeafNode() || n->IsCentralNode() || n->Windows.Size == 0) return nullptr;
    if (ImGui::DockNodeGetRootNode(n)->ID != a.dockId) return nullptr;
    return n;
}

const char* dirName(int d) {
    switch (d) {
        case ImGuiDir_Left: return "left";
        case ImGuiDir_Right: return "right";
        case ImGuiDir_Up: return "top";
        case ImGuiDir_Down: return "bottom";
    }
    return "";
}

ImVec2 clampedDefault(App& a, int w, ImVec2* sizeOut) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float sc = a.settings.uiScale;
    ImVec2 s(a.lay[w].defSize.x * sc, a.lay[w].defSize.y * sc);
    float top = 76 * sc, bottom = 30 * sc;
    s.x = std::min(s.x, vp->Size.x - 20);
    s.y = std::min(s.y, vp->Size.y - top - bottom - 10);
    ImVec2 p(vp->Pos.x + a.lay[w].defPos.x * sc, vp->Pos.y + a.lay[w].defPos.y * sc);
    p.x = std::clamp(p.x, vp->Pos.x + 10, std::max(vp->Pos.x + 10, vp->Pos.x + vp->Size.x - s.x - 10));
    p.y = std::clamp(p.y, vp->Pos.y + top, std::max(vp->Pos.y + top, vp->Pos.y + vp->Size.y - bottom - s.y));
    if (sizeOut) *sizeOut = s;
    return p;
}

}  // namespace

bool App::isDocked(int w) const {
    ImGuiWindow* win = findWin(w);
    return win && win->DockIsActive;
}

bool App::isOutside(int w) const {
    ImGuiWindow* win = findWin(w);
    return win && win->Viewport && win->Viewport != ImGui::GetMainViewport();
}

// ---------------------------------------------------------------- window ---
bool beginWindow(App& app, int w, ImVec2 offset, ImVec2 size) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    WinLayout& L = app.lay[w];
    L.defPos = offset;
    L.defSize = size;
    ImVec2 ds;
    ImVec2 dp = clampedDefault(app, w, &ds);
    if (L.place > 0) {
        L.place--;
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowViewport(vp->ID);
        if (L.atMouse) {
            ImVec2 m = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(m.x - ds.x * 0.3f, m.y - 12), ImGuiCond_Always);
            if (!L.place) L.atMouse = false;
        } else {
            ImGui::SetNextWindowPos(dp, ImGuiCond_Always);
        }
        ImGui::SetNextWindowSize(ds, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(dp, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ds, ImGuiCond_FirstUseEver);
    }
    {
        // set every frame: ImGui keeps a window's class until it is given a new one
        ImGuiWindowClass wc;
        if (L.popped && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&wc);
    }
    if (app.focus[w]) { ImGui::SetNextWindowFocus(); app.focus[w] = false; }
    std::string title = upper(winTitle(w)) + "###" + winTitle(w);
    ImGui::PushFont(app.fonts.title, kFontTitle);
    // NoCollapse: a double-click on the title bar docks instead of folding the window away
    bool r = ImGui::Begin(title.c_str(), &app.open[w], ImGuiWindowFlags_NoCollapse);
    ImGui::PopFont();
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGuiContext& g = *GImGui;
    float fh = ImGui::GetFrameHeight();
    bool openMenu = false;

    if (L.pendingSide >= 0 && !win->DockIsActive && ++L.pendingFrames > 8) L.pendingSide = -1;  // the dock never happened
    if (win->DockIsActive) {
        L.lastDock = win->DockId;
        if (L.pendingSide >= 0) { app.sideNode[L.pendingSide] = win->DockId; L.pendingSide = -1; }
        L.popped = false;
        // the tab: double-click floats the window, right-click opens its menu
        {
            ImRect t = win->DC.DockTabItemRect;
            app.anchors[std::string("tab_") + winCode(w)] = ImVec4(t.Min.x, t.Min.y, t.Max.x, t.Max.y);
        }
        // HoveredRect alone would see through a window floating over the tab
        bool tabHovered = (win->DC.DockTabItemStatusFlags & ImGuiItemStatusFlags_HoveredRect) && win->DockNode &&
                          win->DockNode->HostWindow && g.HoveredWindow == win->DockNode->HostWindow;
        if (tabHovered) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) app.undock(w, true);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) openMenu = true;
            ImGui::SetItemTooltip("Drag to move  ·  double-click to float  ·  right-click for options");
        }
        // a ⋮ button at the right of the tab bar, for the tab on show
        ImGuiDockNode* node = win->DockNode;
        if (node && node->VisibleWindow == win && node->TabBar && node->HostWindow && !node->IsNoTabBar() &&
            !node->IsHiddenTabBar()) {
            ImRect nr = node->Rect();
            float right = nr.Max.x - (node->HasCloseButton ? fh : 0) - 2;
            ImRect bb(ImVec2(right - fh, nr.Min.y), ImVec2(right, nr.Min.y + fh));
            if (node->TabBar->WidthAllTabs + 3 * fh < nr.GetWidth()) {
                bool hov = g.HoveredWindow == node->HostWindow && ImGui::IsMouseHoveringRect(bb.Min, bb.Max, false) &&
                           !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
                ImDrawList* dl = node->HostWindow->DrawList;
                dl->PushClipRect(bb.Min, bb.Max, false);
                if (hov) dl->AddRectFilled(bb.Min, bb.Max, col::rgb(255, 255, 255, 22), 3);
                ImVec2 c = bb.GetCenter();
                for (int i = -1; i <= 1; i++) dl->AddCircleFilled(ImVec2(c.x, c.y + i * 4.5f), 1.7f, hov ? col::Text : col::Dim);
                dl->PopClipRect();
                app.anchors[std::string("opt_") + winCode(w)] = ImVec4(bb.Min.x, bb.Min.y, bb.Max.x, bb.Max.y);
                if (hov) {
                    ImGui::SetTooltip("Window options: dock, float, pop out, reset");
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openMenu = true;
                }
            }
        }
    } else if (!(win->Flags & ImGuiWindowFlags_NoTitleBar) && !win->SkipItems) {
        ImRect tb = win->TitleBarRect();
        ImDrawList* dl = win->DrawList;
        // the livery stripe along the top of the title bar
        dl->PushClipRect(tb.Min, tb.Max, false);
        bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        dl->AddRectFilled(tb.Min, ImVec2(tb.Max.x, tb.Min.y + 2), focused ? col::Accent : withAlpha(col::Accent, 0.35f),
                          ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersTop);
        dl->PopClipRect();
        // the window you are working in firms up; the others stay glass
        if (focused) {
            ImRect rr = win->Rect();
            dl->PushClipRect(rr.Min, rr.Max, false);
            dl->AddRectFilled(ImVec2(rr.Min.x, tb.Max.y), rr.Max, withAlpha(col::Panel, 0.7f), ImGui::GetStyle().WindowRounding,
                              ImDrawFlags_RoundCornersBottom);
            dl->PopClipRect();
        }
        // the ⋮ options button, left of the close button
        ImRect bb(ImVec2(tb.Max.x - 2 * fh - 2, tb.Min.y), ImVec2(tb.Max.x - fh - 2, tb.Max.y));
        ImGuiID bid = win->GetID("##layoutbtn");
        ImGui::PushClipRect(tb.Min, tb.Max, false);
        bool hov = false, held = false, pressed = false;
        if (ImGui::ItemAdd(bb, bid, nullptr, ImGuiItemFlags_NoNav)) pressed = ImGui::ButtonBehavior(bb, bid, &hov, &held);
        ImGui::PopClipRect();
        dl->PushClipRect(tb.Min, tb.Max, false);
        if (hov) dl->AddRectFilled(ImVec2(bb.Min.x + 2, bb.Min.y + 3), ImVec2(bb.Max.x - 2, bb.Max.y - 3), col::rgb(255, 255, 255, held ? 40 : 22), 3);
        ImVec2 c = bb.GetCenter();
        for (int i = -1; i <= 1; i++) dl->AddCircleFilled(ImVec2(c.x, c.y + i * 4.5f), 1.7f, hov ? col::Text : col::Dim);
        // the window's code, left of the options button
        const char* code = winCode(w);
        float fs = 13.0f * ImGui::GetStyle().FontScaleMain;
        ImVec2 ts = g_mono->CalcTextSizeA(fs, FLT_MAX, 0, code);
        float x = bb.Min.x - 6 - ts.x;
        dl->AddText(g_mono, fs, ImVec2(x, tb.Min.y + (tb.GetHeight() - ts.y) * 0.5f + 1), withAlpha(col::Dim, 0.8f), code);
        dl->PopClipRect();
        app.anchors[std::string("opt_") + winCode(w)] = ImVec4(bb.Min.x, bb.Min.y, bb.Max.x, bb.Max.y);
        if (hov) ImGui::SetTooltip("Window options: dock, pop out, reset");
        if (pressed) openMenu = true;
        // shortcuts on the title bar itself (not on its buttons)
        bool onBar = g.HoveredWindow == win && ImGui::IsMouseHoveringRect(tb.Min, ImVec2(bb.Min.x, tb.Max.y), false) && !hov;
        if (onBar && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ImGuiDockNode* last = liveNode(L.lastDock);
            if (last && last->IsLeafNode() && last->Windows.Size > 0) {
                LayoutOp op;
                op.kind = LayoutOp::Node;
                op.w = w;
                op.node = last->ID;
                app.layoutOps.push_back(op);
            } else {
                app.dockSide(w, ImGuiDir_Right);
            }
        }
        if (onBar && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) openMenu = true;
    }
    if (openMenu) ImGui::OpenPopup("##winmenu");
    ImGui::PushFont(app.fonts.ui, kFontUi);
    if (ImGui::BeginPopup("##winmenu")) {
        app.windowMenu(w);
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    return r;
}

// ------------------------------------------------------------ the menus ---
void App::windowMenu(int w) {
    ImGui::PushFont(fonts.monoBold, 13.0f);
    textColored(col::Accent, "%s", winCode(w));
    ImGui::PopFont();
    ImGui::SameLine();
    textDim("%s", winTitle(w));
    ImGui::Separator();
    bool docked = isDocked(w);
    if (ImGui::BeginMenu("Dock")) {
        const int dirs[] = {ImGuiDir_Left, ImGuiDir_Right, ImGuiDir_Down, ImGuiDir_Up};
        const char* labels[] = {"Left column", "Right column", "Bottom row", "Top row"};
        for (int i = 0; i < 4; i++) {
            bool exists = sideColumn(*this, dirs[i]) != nullptr;
            std::string l = std::string(labels[i]) + (exists ? "  (as a tab)" : "");
            if (ImGui::MenuItem(l.c_str())) dockSide(w, dirs[i]);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Dock as a tab with")) {
        int n = 0;
        for (int k = 0; k < W_COUNT; k++) {
            if (k == w || !open[k]) continue;
            n++;
            std::string l = std::string(winCode(k)) + "  " + winTitle(k);
            if (ImGui::MenuItem(l.c_str())) dockWith(w, k);
        }
        if (!n) ImGui::TextDisabled("Open another window first");
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Float (undock)", "double-click tab", false, docked)) undock(w);
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        bool out = isOutside(w) || lay[w].popped;
        if (!out && ImGui::MenuItem("Pop out into its own window")) popOut(w, true);
        if (out && ImGui::MenuItem("Bring back into the game window")) popOut(w, false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A popped-out window can be dragged to any monitor.");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset this window")) {
        if (docked) undock(w);
        lay[w].popped = false;
        lay[w].place = 3;
        lay[w].atMouse = false;
    }
    if (ImGui::MenuItem("Close")) open[w] = false;
    ImGui::Separator();
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("Double-click the title bar to dock back;");
    textDim("double-click a tab to float it.");
    ImGui::PopFont();
}

void App::layoutMenu() {
    if (ImGui::MenuItem("Reset layout", "GO> RESET")) resetLayout(false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every window back at its default place in the game window, nothing docked.\nOpen windows stay open.");
    if (ImGui::MenuItem("Reset and close all windows")) resetLayout(true);
    ImGui::Separator();
    int nOpen = 0;
    for (int k = 0; k < W_COUNT; k++) nOpen += open[k];
    if (ImGui::BeginMenu("Dock every open window", nOpen > 0)) {
        const int dirs[] = {ImGuiDir_Left, ImGuiDir_Right, ImGuiDir_Down};
        const char* labels[] = {"In the left column", "In the right column", "In the bottom row"};
        for (int i = 0; i < 3; i++)
            if (ImGui::MenuItem(labels[i]))
                for (int k = 0; k < W_COUNT; k++)
                    if (open[k]) dockSide(k, dirs[i]);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Float every window", nullptr, false, nOpen > 0))
        for (int k = 0; k < W_COUNT; k++)
            if (open[k] && isDocked(k)) undock(k);
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && ImGui::MenuItem("Bring every window back into the game window"))
        for (int k = 0; k < W_COUNT; k++)
            if (open[k] && (isOutside(k) || lay[k].popped)) popOut(k, false);
    ImGui::Separator();
    // saved layouts
    std::vector<std::string> names = listLayouts();
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("SAVED LAYOUTS");
    ImGui::PopFont();
    if (names.empty()) ImGui::TextDisabled("None yet");
    for (auto& n : names) {
        ImGui::PushID(n.c_str());
        if (ImGui::MenuItem(n.c_str())) {
            std::string err;
            if (!loadLayout(n, &err)) toast(err, false);
        }
        if (ImGui::BeginPopupContextItem("del")) {
            if (ImGui::MenuItem("Delete this layout")) deleteLayout(n);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    static char nameBuf[48] = "";
    ImGui::SetNextItemWidth(190 * settings.uiScale);
    bool enter = ImGui::InputTextWithHint("##lname", "Name, e.g. Trading", nameBuf, sizeof nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::SmallButton("Save current") || enter) && nameBuf[0]) {
        std::string err;
        if (saveLayout(nameBuf, &err)) { toast(std::string("Layout saved: ") + nameBuf); nameBuf[0] = 0; }
        else toast(err, false);
    }
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("A saved layout keeps which windows are open and where. Right-click one to delete it.");
    ImGui::PopFont();
    ImGui::Separator();
    if (ImGui::Checkbox("Only dock while holding Shift", &settings.dockShift)) {
        ImGui::GetIO().ConfigDockingWithShift = settings.dockShift;
        saveSettings();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops windows docking by accident when you drag them around.");
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("Drag a title bar onto the arrows that appear to dock;");
    textDim("drag a tab out, or double-click it, to float it again.");
    ImGui::PopFont();
}

// --------------------------------------------------------------- actions ---
void App::resetLayout(bool closeAll) {
    resetQueued = true;
    resetCloseAll = closeAll;
}

void App::dockSide(int w, int dir) {
    LayoutOp op;
    op.kind = LayoutOp::Side;
    op.w = w;
    op.dir = dir;
    layoutOps.push_back(op);
}

void App::dockWith(int w, int target) {
    LayoutOp op;
    op.kind = LayoutOp::With;
    op.w = w;
    op.target = target;
    layoutOps.push_back(op);
}

void App::undock(int w, bool atMouse) {
    LayoutOp op;
    op.kind = LayoutOp::Undock;
    op.w = w;
    layoutOps.push_back(op);
    lay[w].atMouse = atMouse;
}

void App::popOut(int w, bool out) {
    LayoutOp op;
    op.kind = out ? LayoutOp::PopOut : LayoutOp::PopIn;
    op.w = w;
    layoutOps.push_back(op);
}

// Called each frame before the windows: turns queued actions into ImGui dock requests.
// A docked window is floated first and docked again on a later frame.
void App::processLayoutOps() {
    if (layoutOps.empty()) return;
    ImGuiContext* ctx = GImGui;
    std::vector<LayoutOp> later;
    for (LayoutOp& op : layoutOps) {
        if (!open[op.w] || ++op.age > 40) continue;
        ImGuiWindow* win = findWin(op.w);
        if (!win || win->LastFrameActive < ctx->FrameCount - 1) { later.push_back(op); continue; }  // not drawn yet
        bool docked = win->DockIsActive || win->DockNode != nullptr;
        switch (op.kind) {
            case LayoutOp::Undock:
                if (docked) {
                    ImGui::DockContextQueueUndockWindow(ctx, win);
                    lay[op.w].place = 2;
                }
                break;
            case LayoutOp::PopOut:
                if (docked) { ImGui::DockContextQueueUndockWindow(ctx, win); later.push_back(op); break; }
                lay[op.w].popped = true;
                ImGui::SetWindowPos(win, ImVec2(win->Pos.x + 40, win->Pos.y + 40));
                break;
            case LayoutOp::PopIn:
                lay[op.w].popped = false;
                lay[op.w].place = 2;
                lay[op.w].atMouse = false;
                break;
            case LayoutOp::Side: {
                ImGuiDockNode* col = sideColumn(*this, op.dir);
                if (docked) {
                    if (col && win->DockNode == col) break;  // already there
                    ImGui::DockContextQueueUndockWindow(ctx, win);
                    later.push_back(op);
                    break;
                }
                lay[op.w].popped = false;
                if (col) {
                    ImGui::DockContextQueueDock(ctx, col->HostWindow, col, win, ImGuiDir_None, 0.0f, false);
                    // more than one op can target a fresh column in the same frame: wait for it to exist
                } else {
                    // another window is already on its way to make this column: follow it next frame
                    bool making = false;
                    for (int k = 0; k < W_COUNT; k++)
                        if (k != op.w && lay[k].pendingSide == op.dir) making = true;
                    if (making) { later.push_back(op); break; }
                    ImGuiDockNode* root = liveNode(dockId);
                    if (!root || !root->HostWindow) { later.push_back(op); break; }
                    float share = op.dir == ImGuiDir_Left || op.dir == ImGuiDir_Right ? 0.30f : 0.34f;
                    float ratio = op.dir == ImGuiDir_Right || op.dir == ImGuiDir_Down ? 1.0f - share : share;
                    ImGui::DockContextQueueDock(ctx, root->HostWindow, root, win, (ImGuiDir)op.dir, ratio, true);
                    lay[op.w].pendingSide = op.dir;
                    lay[op.w].pendingFrames = 0;
                }
                break;
            }
            case LayoutOp::With: {
                ImGuiWindow* tw = findWin(op.target);
                if (!tw || !open[op.target]) break;
                if (tw->DockNode && win->DockNode == tw->DockNode) break;
                if (docked) { ImGui::DockContextQueueUndockWindow(ctx, win); later.push_back(op); break; }
                lay[op.w].popped = false;
                ImGui::DockContextQueueDock(ctx, tw, tw->DockIsActive ? tw->DockNode : nullptr, win, ImGuiDir_None, 0.0f, false);
                break;
            }
            case LayoutOp::Node: {
                ImGuiDockNode* n = liveNode(op.node);
                if (!n || !n->IsLeafNode()) { dockSide(op.w, ImGuiDir_Right); break; }
                if (docked) break;
                lay[op.w].popped = false;
                ImGuiWindow* host = n->HostWindow ? n->HostWindow : (n->Windows.Size ? n->Windows[0] : nullptr);
                if (!host) { dockSide(op.w, ImGuiDir_Right); break; }
                ImGui::DockContextQueueDock(ctx, host, n, win, ImGuiDir_None, 0.0f, false);
                break;
            }
        }
    }
    layoutOps = later;
    // a side op that re-queued itself must not spin forever on a window that never docks
    if (layoutOps.size() > 64) layoutOps.clear();
}

// Between frames: whole-layout changes that ImGui cannot make mid-frame.
void App::preFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigDockingWithShift = settings.dockShift;
    if (resetQueued) {
        resetQueued = false;
        ImGui::DockContextClearNodes(GImGui, 0, true);  // every dock node, docked windows float again
        for (int k = 0; k < 4; k++) sideNode[k] = 0;
        layoutOps.clear();
        for (int k = 0; k < W_COUNT; k++) {
            lay[k].popped = false;
            lay[k].place = 3;
            lay[k].atMouse = false;
            lay[k].lastDock = 0;
            lay[k].pendingSide = -1;
            if (resetCloseAll) open[k] = false;
        }
        settings.popped = 0;
        toast(resetCloseAll ? "Layout reset; windows closed" : "Layout reset: every window is back at its default place");
    }
    if (!pendingLayout.empty()) {
        ImGui::DockContextClearNodes(GImGui, 0, true);
        for (int k = 0; k < 4; k++) sideNode[k] = 0;
        layoutOps.clear();
        ImGui::LoadIniSettingsFromMemory(pendingLayout.c_str(), pendingLayout.size());
        pendingLayout.clear();
        if (pendingLayoutHasOpen)
            for (int k = 0; k < W_COUNT; k++) open[k] = pendingOpen[k];
        for (int k = 0; k < W_COUNT; k++) {
            lay[k].popped = (settings.popped >> k) & 1;
            lay[k].place = 0;
            lay[k].pendingSide = -1;
        }
    }
    uint32_t mask = 0;
    for (int k = 0; k < W_COUNT; k++)
        if (lay[k].popped) mask |= 1u << k;
    settings.popped = mask;
}

// ------------------------------------------------------- saved layouts ---
static fs::path layoutDir(const std::string& dataDir) { return fs::path(dataDir) / "layouts"; }

static std::string safeName(const std::string& n) {
    std::string s;
    for (char c : n)
        if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') s += c;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s.substr(0, 40);
}

bool App::saveLayout(const std::string& name, std::string* err) {
    std::string n = safeName(name);
    if (n.empty()) { if (err) *err = "Give the layout a name (letters and numbers)."; return false; }
    std::error_code ec;
    fs::create_directories(layoutDir(dataDir), ec);
    size_t sz = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&sz);
    std::ofstream f(layoutDir(dataDir) / (n + ".layout"), std::ios::binary);
    if (!f) { if (err) *err = "Could not write the layout file."; return false; }
    uint32_t mask = 0;
    for (int k = 0; k < W_COUNT; k++)
        if (lay[k].popped) mask |= 1u << k;
    f << "nebulaone-layout 1\nopen=";
    for (int k = 0; k < W_COUNT; k++) f << (open[k] ? '1' : '0');
    f << "\npopped=" << mask << "\n---\n";
    f.write(ini, (std::streamsize)sz);
    return (bool)f;
}

bool App::loadLayout(const std::string& name, std::string* err) {
    std::ifstream f(layoutDir(dataDir) / (safeName(name) + ".layout"), std::ios::binary);
    if (!f) { if (err) *err = "That layout is gone."; return false; }
    std::string line;
    std::getline(f, line);
    if (line.rfind("nebulaone-layout", 0) != 0) { if (err) *err = "Not a NebulaOne layout file."; return false; }
    pendingLayoutHasOpen = false;
    while (std::getline(f, line) && line != "---") {
        if (line.rfind("open=", 0) == 0) {
            std::string v = line.substr(5);
            for (int k = 0; k < W_COUNT; k++) pendingOpen[k] = k < (int)v.size() && v[k] == '1';
            pendingLayoutHasOpen = true;
        }
        if (line.rfind("popped=", 0) == 0) settings.popped = (uint32_t)std::strtoul(line.c_str() + 7, nullptr, 10);
    }
    std::stringstream rest;
    rest << f.rdbuf();
    pendingLayout = rest.str();
    if (pendingLayout.empty()) pendingLayout = "\n";
    toast("Layout: " + safeName(name));
    return true;
}

std::vector<std::string> App::listLayouts() const {
    std::vector<std::string> v;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(layoutDir(dataDir), ec))
        if (e.path().extension() == ".layout") v.push_back(e.path().stem().string());
    std::sort(v.begin(), v.end());
    return v;
}

void App::deleteLayout(const std::string& name) {
    std::error_code ec;
    fs::remove(layoutDir(dataDir) / (safeName(name) + ".layout"), ec);
}

}  // namespace nb
