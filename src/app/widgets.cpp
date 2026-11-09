#include "app/widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "imgui_internal.h"

namespace nb {

std::string upper(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

// ------------------------------------------------------------------- text --
void textColored(ImU32 c, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextV(fmt, a);
    ImGui::PopStyleColor();
    va_end(a);
}
void textDim(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Dim);
    ImGui::TextV(fmt, a);
    ImGui::PopStyleColor();
    va_end(a);
}

void header(const char* label) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, kFontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Dim);
    std::string u = upper(label);
    ImGui::SeparatorText(u.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

ImFont* g_mono = nullptr;
static ImFont* monoFont() { return g_mono; }

void monoText(const std::string& s, ImU32 c) {
    ImGui::PushFont(monoFont(), kFontMono);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void rightText(const std::string& s, ImU32 c, bool mono) {
    if (mono) ImGui::PushFont(monoFont(), kFontMono);
    float w = ImGui::CalcTextSize(s.c_str()).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    if (mono) ImGui::PopFont();
}

void cellMoney(Money m, bool signColor) {
    ImU32 c = !signColor ? col::Text : m > 0 ? col::Good : m < 0 ? col::Bad : col::Dim;
    rightText(fmtMoney(m, false), c);
}
void cellInt(int64_t v) { rightText(fmtInt(v)); }
void cellQty(double v) { rightText(fmtQty(v)); }

void zoneBadge(Zone z) {
    ImGui::PushFont(monoFont(), kFontMono);
    ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(z));
    ImGui::TextUnformatted(upper(zoneName(z)).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string riskLabel(const StarSystem& s) {
    std::string g = riskGrade(s.risk);
    int now = riskGradeIndex(s.risk), next = riskGradeIndex(s.riskLive);
    if (next > now) g += " v";
    else if (next < now) g += " ^";
    return g;
}

void riskBadge(float risk, float live) {
    ImGui::PushFont(monoFont(), kFontMono);
    ImGui::PushStyleColor(ImGuiCol_Text, riskColor(risk));
    ImGui::TextUnformatted(riskGrade(risk));
    ImGui::PopStyleColor();
    if (live >= 0) {
        int now = riskGradeIndex(risk), next = riskGradeIndex(live);
        if (now != next) {
            ImGui::SameLine(0, 3);
            ImGui::PushStyleColor(ImGuiCol_Text, next > now ? col::Bad : col::Good);
            ImGui::TextUnformatted(next > now ? "v" : "^");
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopFont();
    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("Underwriters' rating %s (risk %.0f of 100).", riskGrade(risk), risk);
        if (live >= 0) ImGui::Text("Today's figure: %s (%.0f). Ratings are republished weekly.", riskGrade(live), live);
        ImGui::TextDisabled("It prices hull and cargo insurance and steers Lowest-risk routes.");
        ImGui::EndTooltip();
    }
}

void helpMark(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void kpi(const char* label, const std::string& value, ImU32 c, float width) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, kFontSmall);
    textDim("%s", upper(label).c_str());
    ImGui::PopFont();
    ImGui::PushFont(monoFont(), 20.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (width > 0) ImGui::Dummy(ImVec2(width, 0));
    ImGui::EndGroup();
}

bool liveryPicker(const char* id, int& livery) {
    bool changed = false;
    ImGui::PushID(id);
    float sz = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < LIVERY_COUNT; i++) {
        if (i) ImGui::SameLine(0, 5);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("sw", ImVec2(sz, sz))) { livery = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + sz - 3, p.y + sz - 3), liveryColor(i), 2);
        if (i == livery) dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), col::Text, 3, 0, 1.5f);
        else if (hov) dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), withAlpha(col::Text, 0.4f), 3);
        if (hov) ImGui::SetTooltip("%s", liveryName(i));
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool accentButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(col::Accent, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(col::Accent, 0.48f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(col::Accent, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, col::Text);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void progressBar(float f, const ImVec2& size, ImU32 c, const char* overlay) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x;
    float h = size.y > 0 ? size.y : ImGui::GetTextLineHeight() * 0.55f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p.y + (ImGui::GetTextLineHeight() - h) * 0.5f;
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), IM_COL32(255, 255, 255, 18), 1);
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w * std::clamp(f, 0.0f, 1.0f), y + h), c, 1);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    if (overlay) {
        ImGui::SameLine();
        textDim("%s", overlay);
    }
}

// ---------------------------------------------------------------- pickers --
std::string App::stationLabel(Id st) const {
    if (st == NONE) return "(choose a station)";
    const Station& s = world.stations[st];
    const StarSystem& sy = world.systems[s.system];
    char b[24];
    std::snprintf(b, sizeof b, "%-3s %-4s ", upper(zoneName(sy.zone)).substr(0, 3).c_str(), riskGrade(sy.risk));
    return std::string(b) + s.name;
}

// Pickers list the nearest few hundred entries unless a filter is typed, so they stay quick on huge charts.
static constexpr size_t kPickerCap = 300;

bool stationPicker(App& app, const char* id, Id& station, float width, const std::function<bool(const Station&)>& filter) {
    World& w = app.world;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    std::string preview = app.stationLabel(station);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char buf[64] = "";
        if (ImGui::IsWindowAppearing()) { buf[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "filter by name, type, region or hex", buf, sizeof buf);
        std::string f = upper(buf);
        Id homeSys = w.stations[w.pc().home].system;
        const auto& row = w.jumpsFrom(homeSys);
        std::vector<Id> order;
        for (auto& s : w.stations) {
            if (s.dead) continue;
            if (filter && !filter(s)) continue;
            if (!f.empty()) {
                const StarSystem& sy = w.systems[s.system];
                std::string hay = upper(s.name + " " + stationTypeName(s.type) + " " + w.regions[sy.region].name + " " + sy.hexId);
                if (hay.find(f) == std::string::npos) continue;
            }
            order.push_back(s.id);
        }
        std::sort(order.begin(), order.end(), [&](Id a, Id b) {
            int da = row[w.stations[a].system], db = row[w.stations[b].system];
            return da != db ? da < db : w.stations[a].name < w.stations[b].name;
        });
        size_t total = order.size();
        if (order.size() > kPickerCap) order.resize(kPickerCap);
        ImGuiListClipper clip;
        clip.Begin((int)order.size());
        while (clip.Step())
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                Id s = order[k];
                const Station& st = w.stations[s];
                const StarSystem& sy = w.systems[st.system];
                ImGui::PushID(s);
                ImGui::PushFont(g_mono, kFontMono);
                ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(sy.zone));
                ImGui::TextUnformatted(upper(zoneName(sy.zone)).substr(0, 3).c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 6);
                ImGui::PushStyleColor(ImGuiCol_Text, riskColor(sy.risk));
                ImGui::Text("%-3s", riskGrade(sy.risk));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                if (ImGui::Selectable(st.name.c_str(), station == s, 0, ImVec2(0, 0))) { station = s; changed = true; }
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 130);
                textDim("%3dj  %s", row[st.system], stationTypeName(st.type));
                ImGui::PopID();
            }
        if (total > order.size()) textDim("%d more further away: type to filter", (int)(total - order.size()));
        ImGui::EndCombo();
    }
    return changed;
}

bool systemPicker(App& app, const char* id, Id& system, float width) {
    World& w = app.world;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    std::string preview = system == NONE ? "(choose a system)" : w.systems[system].name + "  " + w.systems[system].hexId;
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char buf[64] = "";
        if (ImGui::IsWindowAppearing()) { buf[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "filter by name, region or hex", buf, sizeof buf);
        std::string f = upper(buf);
        Id homeSys = w.stations[w.pc().home].system;
        const auto& row = w.jumpsFrom(homeSys);
        std::vector<Id> order;
        for (auto& s : w.systems)
            if (f.empty() || upper(s.name + " " + w.regions[s.region].name + " " + s.hexId).find(f) != std::string::npos) order.push_back(s.id);
        std::sort(order.begin(), order.end(), [&](Id a, Id b) { return row[a] != row[b] ? row[a] < row[b] : a < b; });
        size_t total = order.size();
        if (order.size() > kPickerCap) order.resize(kPickerCap);
        ImGuiListClipper clip;
        clip.Begin((int)order.size());
        while (clip.Step())
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                Id s = order[k];
                const StarSystem& sy = w.systems[s];
                ImGui::PushID(s);
                ImGui::PushFont(g_mono, kFontMono);
                ImGui::PushStyleColor(ImGuiCol_Text, zoneColor(sy.zone));
                ImGui::TextUnformatted(upper(zoneName(sy.zone)).substr(0, 3).c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 6);
                ImGui::PushStyleColor(ImGuiCol_Text, riskColor(sy.risk));
                ImGui::Text("%-3s", riskGrade(sy.risk));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                if (ImGui::Selectable(sy.name.c_str(), system == s)) { system = s; changed = true; }
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 170);
                textDim("%3dj  %d belts  %d st  %s", row[s], (int)sy.belts.size(), (int)sy.stations.size(), sy.hexId.c_str());
                ImGui::PopID();
            }
        if (total > order.size()) textDim("%d more further away: type to filter", (int)(total - order.size()));
        ImGui::EndCombo();
    }
    return changed;
}

bool itemPicker(const char* id, int& it, float width, bool allowAny, bool tradableOnly) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const char* preview = it < 0 ? "Everything" : item(it).name.c_str();
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) {
        if (allowAny && ImGui::Selectable("Everything", it < 0)) { it = -1; changed = true; }
        ItemCat last = ItemCat::Blueprint;
        bool first = true;
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (tradableOnly && (isShipItem(i) || isBpo(i) || isBpc(i) || isStructure(i))) continue;
            if (first || item(i).cat != last) {
                textDim("%s", upper(catName(item(i).cat)).c_str());
                last = item(i).cat;
                first = false;
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(("   " + item(i).name).c_str(), it == i)) { it = i; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool corpPicker(App& app, const char* id, Id& corp, float width, bool allowAny) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const char* preview = corp == NONE ? "Anyone not friendly" : app.world.corps[corp].name.c_str();
    if (ImGui::BeginCombo(id, preview)) {
        if (allowAny && ImGui::Selectable("Anyone not friendly", corp == NONE)) { corp = NONE; changed = true; }
        for (auto& c : app.world.corps) {
            if (c.player || !c.alive) continue;
            if (app.world.corps.size() > 60 && !(app.world.isOutlaw(c.id) || app.world.standing(c.id, app.world.player) < -2 ||
                                                 app.world.pc().shares.count(c.id)))
                continue;  // on big charts, only the corporations that matter to you
            ImGui::PushID(c.id);
            if (ImGui::Selectable((c.name + "  [" + c.ticker + "]").c_str(), corp == c.id)) { corp = c.id; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool inputCredits(const char* label, Money& value, float width) {
    double v = toCr(value);
    ImGui::SetNextItemWidth(width);
    bool r = ImGui::InputDouble(label, &v, 0, 0, "%.2f");
    if (r) value = credits(std::max(0.0, v));
    return r;
}

bool inputQty(const char* label, int64_t& value, float width) {
    ImGui::SetNextItemWidth(width);
    ImS64 v = value;
    bool r = ImGui::InputScalar(label, ImGuiDataType_S64, &v, nullptr, nullptr, "%lld");
    if (r) value = std::max<ImS64>(0, v);
    return r;
}

bool beginTable(const char* id, int cols, ImGuiTableFlags extra, ImVec2 size) {
    ImGuiTableFlags f = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH |
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | extra;
    return ImGui::BeginTable(id, cols, f, size);
}

bool sortSpec(int& column, bool& ascending) {
    ImGuiTableSortSpecs* s = ImGui::TableGetSortSpecs();
    if (!s || s->SpecsCount == 0) return false;
    column = s->Specs[0].ColumnIndex;
    ascending = s->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    return true;
}

}  // namespace nb
