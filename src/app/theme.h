// NebulaOne - colours, fonts and the look: smoked warm-grey glass over a
// deep-space chart, condensed type, and one accent colour: your corporation's
// livery. Amber is kept for warnings.
#pragma once
#include "core/types.h"
#include "imgui.h"

namespace nb {

namespace col {
constexpr ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
constexpr ImU32 Bg = rgb(7, 9, 13);            // deep space behind the chart
constexpr ImU32 Panel = rgb(21, 23, 27);       // glass, before transparency
constexpr ImU32 PanelHi = rgb(32, 35, 40);     // headers, toasts
constexpr ImU32 Border = rgb(70, 72, 76);
constexpr ImU32 Rule = rgb(48, 50, 54);        // light ledger rules
constexpr ImU32 Text = rgb(230, 228, 222);     // warm white
constexpr ImU32 Dim = rgb(140, 142, 146);
constexpr ImU32 Warn = rgb(242, 168, 64);      // amber: alerts only
constexpr ImU32 Bad = rgb(228, 92, 78);
constexpr ImU32 Good = rgb(116, 192, 118);
constexpr ImU32 Neutral = rgb(162, 162, 160);
constexpr ImU32 Info = rgb(126, 164, 214);     // links
inline ImU32 Accent = rgb(70, 196, 178);       // the player's livery (setLivery)
inline ImU32 AccentDim = rgb(35, 98, 89);
inline ImU32 OnAccent = rgb(8, 14, 14);        // text drawn on an accent fill
}  // namespace col

// Corporate liveries: the player picks one; it colours the interface, their
// fleets and their outposts.
constexpr int LIVERY_COUNT = 8;
const char* liveryName(int i);
ImU32 liveryColor(int i, int alpha = 255);
void setLivery(int i);  // sets col::Accent and re-applies the theme colours

ImU32 zoneColor(Zone z, int alpha = 255);
ImU32 riskColor(float risk, int alpha = 255);  // green AAA .. red D
ImU32 withAlpha(ImU32 c, float a);
ImU32 mix(ImU32 a, ImU32 b, float t);

void applyTheme(float scale);

// How see-through the floating windows are (0 = solid).
constexpr float kGlass = 0.10f;

// Font sizes (unscaled)
constexpr float kFontUi = 16.5f;
constexpr float kFontSmall = 14.0f;
constexpr float kFontHead = 18.0f;
constexpr float kFontMono = 15.5f;
constexpr float kFontTitle = 15.0f;

}  // namespace nb
