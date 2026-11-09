#include "app/theme.h"

#include <algorithm>
#include <cmath>

#include "implot.h"

namespace nb {

ImU32 withAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

static const struct { const char* name; int r, g, b; } kLiveries[LIVERY_COUNT] = {
    {"Signal teal", 70, 196, 178}, {"Cobalt", 98, 150, 244}, {"Violet", 166, 132, 242}, {"Rose", 232, 118, 156},
    {"Copper", 218, 132, 86},      {"Gold", 226, 190, 92},   {"Lime", 166, 206, 88},    {"Ivory", 226, 222, 206},
};

const char* liveryName(int i) { return kLiveries[std::clamp(i, 0, LIVERY_COUNT - 1)].name; }
ImU32 liveryColor(int i, int alpha) {
    const auto& l = kLiveries[std::clamp(i, 0, LIVERY_COUNT - 1)];
    return IM_COL32(l.r, l.g, l.b, alpha);
}

static float g_scale = 1.0f;
static int g_livery = -1;

void setLivery(int i) {
    i = std::clamp(i, 0, LIVERY_COUNT - 1);
    if (i == g_livery) return;
    g_livery = i;
    col::Accent = liveryColor(i);
    col::AccentDim = mix(col::Accent, col::Bg, 0.5f);
    applyTheme(g_scale);
}

ImU32 zoneColor(Zone z, int alpha) {
    switch (z) {
        case Zone::Charter: return IM_COL32(124, 158, 210, alpha);   // slate blue
        case Zone::Contract: return IM_COL32(212, 170, 88, alpha);   // ochre
        case Zone::Frontier: return IM_COL32(200, 94, 68, alpha);    // rust
    }
    return IM_COL32(200, 200, 200, alpha);
}

ImU32 riskColor(float risk, int alpha) {
    static const int stops[RISK_GRADES][3] = {{110, 192, 116}, {140, 194, 106}, {176, 196, 98}, {214, 196, 92},
                                              {234, 172, 76},  {234, 138, 66},  {228, 104, 70}, {206, 64, 72}};
    int g = riskGradeIndex(risk);
    return IM_COL32(stops[g][0], stops[g][1], stops[g][2], alpha);
}

void applyTheme(float scale) {
    g_scale = scale;
    if (g_livery < 0) g_livery = 0;
    ImGuiStyle& s = ImGui::GetStyle();
    float fontScale = s.FontScaleMain;
    s = ImGuiStyle();
    s.FontScaleMain = fontScale;
    s.WindowPadding = ImVec2(12, 9);
    s.FramePadding = ImVec2(8, 3);
    s.CellPadding = ImVec2(6, 2);
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(5, 4);
    s.IndentSpacing = 14;
    s.ScrollbarSize = 8;
    s.GrabMinSize = 8;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.WindowRounding = 3;
    s.ChildRounding = 2;
    s.FrameRounding = 2;
    s.PopupRounding = 3;
    s.ScrollbarRounding = 4;
    s.GrabRounding = 2;
    s.TabRounding = 2;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 3);
    s.DockingSeparatorSize = 2;
    s.TabBarOverlineSize = 2;

    ImVec4* c = s.Colors;
    auto V = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
    auto A = [](float a) { ImVec4 v = col::v4(col::Accent); v.w = a; return v; };
    const float glass = 1.0f - kGlass;
    c[ImGuiCol_Text] = col::v4(col::Text);
    c[ImGuiCol_TextDisabled] = col::v4(col::Dim);
    c[ImGuiCol_WindowBg] = V(20, 22, 26, glass);
    c[ImGuiCol_ChildBg] = V(0, 0, 0, 0.0f);
    c[ImGuiCol_PopupBg] = V(24, 26, 31, 0.97f);
    c[ImGuiCol_Border] = V(255, 250, 240, 0.13f);
    c[ImGuiCol_BorderShadow] = V(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = V(255, 255, 255, 0.055f);
    c[ImGuiCol_FrameBgHovered] = V(255, 255, 255, 0.09f);
    c[ImGuiCol_FrameBgActive] = A(0.20f);
    c[ImGuiCol_TitleBg] = V(28, 30, 35, glass);
    c[ImGuiCol_TitleBgActive] = V(34, 37, 43, 0.95f);
    c[ImGuiCol_TitleBgCollapsed] = V(28, 30, 35, glass);
    c[ImGuiCol_MenuBarBg] = V(28, 30, 35, 0.9f);
    c[ImGuiCol_ScrollbarBg] = V(0, 0, 0, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = V(255, 255, 255, 0.14f);
    c[ImGuiCol_ScrollbarGrabHovered] = V(255, 255, 255, 0.24f);
    c[ImGuiCol_ScrollbarGrabActive] = A(0.8f);
    c[ImGuiCol_CheckMark] = A(1.0f);
    c[ImGuiCol_SliderGrab] = A(0.8f);
    c[ImGuiCol_SliderGrabActive] = A(1.0f);
    c[ImGuiCol_Button] = V(255, 255, 255, 0.07f);
    c[ImGuiCol_ButtonHovered] = A(0.30f);
    c[ImGuiCol_ButtonActive] = A(0.50f);
    c[ImGuiCol_Header] = A(0.18f);
    c[ImGuiCol_HeaderHovered] = A(0.26f);
    c[ImGuiCol_HeaderActive] = A(0.36f);
    c[ImGuiCol_Separator] = V(255, 250, 240, 0.12f);
    c[ImGuiCol_SeparatorHovered] = A(0.6f);
    c[ImGuiCol_SeparatorActive] = A(1.0f);
    c[ImGuiCol_ResizeGrip] = A(0.10f);
    c[ImGuiCol_ResizeGripHovered] = A(0.45f);
    c[ImGuiCol_ResizeGripActive] = A(0.8f);
    c[ImGuiCol_InputTextCursor] = A(1.0f);
    c[ImGuiCol_Tab] = V(24, 26, 30, glass);
    c[ImGuiCol_TabHovered] = A(0.28f);
    c[ImGuiCol_TabSelected] = V(36, 39, 45, 0.95f);
    c[ImGuiCol_TabSelectedOverline] = A(1.0f);
    c[ImGuiCol_TabDimmed] = V(22, 24, 28, glass);
    c[ImGuiCol_TabDimmedSelected] = V(30, 33, 38, glass);
    c[ImGuiCol_TabDimmedSelectedOverline] = A(0.45f);
    c[ImGuiCol_DockingPreview] = A(0.30f);
    c[ImGuiCol_DockingEmptyBg] = V(0, 0, 0, 0.0f);
    c[ImGuiCol_PlotLines] = A(1.0f);
    c[ImGuiCol_PlotHistogram] = A(0.7f);
    c[ImGuiCol_TableHeaderBg] = V(255, 255, 255, 0.05f);
    c[ImGuiCol_TableBorderStrong] = V(255, 250, 240, 0.12f);
    c[ImGuiCol_TableBorderLight] = V(255, 250, 240, 0.06f);
    c[ImGuiCol_TableRowBg] = V(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = V(255, 255, 255, 0.025f);
    c[ImGuiCol_TextLink] = col::v4(col::Info);
    c[ImGuiCol_TextSelectedBg] = A(0.3f);
    c[ImGuiCol_DragDropTarget] = A(0.9f);
    c[ImGuiCol_NavCursor] = A(0.8f);
    c[ImGuiCol_ModalWindowDimBg] = V(2, 3, 6, 0.55f);
    s.ScaleAllSizes(scale);

    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.Colors[ImPlotCol_FrameBg] = V(0, 0, 0, 0);
    ps.Colors[ImPlotCol_PlotBg] = V(0, 0, 0, 0.25f);
    ps.Colors[ImPlotCol_PlotBorder] = V(255, 250, 240, 0.12f);
    ps.Colors[ImPlotCol_LegendBg] = V(20, 22, 26, 0.92f);
    ps.Colors[ImPlotCol_LegendBorder] = V(255, 250, 240, 0.12f);
    ps.Colors[ImPlotCol_AxisText] = col::v4(col::Dim);
    ps.Colors[ImPlotCol_AxisGrid] = V(255, 255, 255, 0.06f);
    ps.Colors[ImPlotCol_AxisTick] = V(255, 255, 255, 0.10f);
    ps.PlotPadding = ImVec2(6, 6);
}

}  // namespace nb
