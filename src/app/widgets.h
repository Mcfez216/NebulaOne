// NebulaOne - small UI building blocks shared by every window.
#pragma once
#include <functional>
#include <string>

#include "app/app.h"
#include "app/theme.h"
#include "imgui.h"

namespace nb {

extern ImFont* g_mono;  // set by App::init
const unsigned char* embedded(const char* name, size_t* size);
bool createDesktopShortcut(std::string* where, std::string* err);  // platform.cpp


bool liveryPicker(const char* id, int& livery);  // a row of colour swatches

// text
void textColored(ImU32 c, const char* fmt, ...);
void textDim(const char* fmt, ...);
void header(const char* label);               // small uppercase section header with a rule
void monoText(const std::string& s, ImU32 c = col::Text);
void rightText(const std::string& s, ImU32 c = col::Text, bool mono = true);  // right-aligned in the current cell
void cellMoney(Money m, bool signColor = false);
void cellInt(int64_t v);
void cellQty(double v);
void zoneBadge(Zone z);                         // "CHARTER" in the zone's colour
void riskBadge(float risk, float live = -1);    // "BBB" with an outlook arrow when live is given
std::string riskLabel(const StarSystem& s);     // "BBB" + outlook arrow as text
void helpMark(const char* text);
void kpi(const char* label, const std::string& value, ImU32 c = col::Text, float width = 0);
bool accentButton(const char* label, const ImVec2& size = ImVec2(0, 0));
void progressBar(float f, const ImVec2& size, ImU32 c = col::Accent, const char* overlay = nullptr);

// pickers (return true when the value changed)
bool stationPicker(App& app, const char* id, Id& station, float width = 260.0f,
                   const std::function<bool(const Station&)>& filter = nullptr);
bool systemPicker(App& app, const char* id, Id& system, float width = 220.0f);
bool itemPicker(const char* id, int& item, float width = 200.0f, bool allowAny = false, bool tradableOnly = false);
bool corpPicker(App& app, const char* id, Id& corp, float width = 220.0f, bool allowAny = true);

// numeric input helpers on credits
bool inputCredits(const char* label, Money& value, float width = 140.0f);
bool inputQty(const char* label, int64_t& value, float width = 120.0f);

// table helpers
bool beginTable(const char* id, int cols, ImGuiTableFlags extra = 0, ImVec2 size = ImVec2(0, 0));
// For sortable tables: returns sort column (or -1) and direction.
bool sortSpec(int& column, bool& ascending);

std::string upper(std::string s);

// Begin one of the game's windows (caller must ImGui::End() when this returns true or false).
bool beginWindow(App& app, int w, ImVec2 offset, ImVec2 size);

}  // namespace nb
