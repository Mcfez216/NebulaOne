// NebulaOne - title screen.
#include <algorithm>
#include <filesystem>

#include "app/app.h"
#include "app/theme.h"
#include "app/widgets.h"

namespace nb {

void App::drawTitle() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    ImVec2 P = vp->Pos, S = vp->Size;
    // vignette
    for (int i = 0; i < 6; i++) {
        float a = 0.10f + i * 0.02f;
        float inset = i * 40.0f;
        (void)inset;
        dl->AddRectFilledMultiColor(P, ImVec2(P.x + S.x, P.y + S.y * 0.25f), IM_COL32(0, 0, 0, (int)(a * 120)),
                                    IM_COL32(0, 0, 0, (int)(a * 120)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    }
    float aspect = tex.logoSize.y / std::max(1.0f, tex.logoSize.x);
    float lw = std::min({560.0f * settings.uiScale, S.x * 0.42f, S.y * 0.42f / aspect});
    float lh = lw * aspect;
    float y0 = P.y + S.y * 0.08f;
    float breathe = 0.92f + 0.08f * (float)std::sin(time * 0.8);
    if (tex.logo)
        dl->AddImage(ImTextureRef(tex.logo), ImVec2(P.x + (S.x - lw) * 0.5f, y0), ImVec2(P.x + (S.x + lw) * 0.5f, y0 + lh),
                     ImVec2(0, 0), ImVec2(1, 1), withAlpha(IM_COL32(255, 255, 255, 255), breathe));
    // strapline
    const char* tag = "RUN THE COMPANY. OWN THE SPACE BETWEEN.";
    ImFont* f = fonts.brand ? fonts.brand : ImGui::GetFont();
    float ts = 12.0f * settings.uiScale;
    ImVec2 tsz = f->CalcTextSizeA(ts, FLT_MAX, 0, tag);
    dl->AddText(f, ts, ImVec2(P.x + (S.x - tsz.x) * 0.5f, y0 + lh + 14), withAlpha(col::Text, 0.65f), tag);

    // menu
    float bw = 280 * settings.uiScale, bh = 38 * settings.uiScale;
    ImGui::SetNextWindowPos(ImVec2(P.x + (S.x - bw) * 0.5f - 10, y0 + lh + 44));
    ImGui::SetNextWindowSize(ImVec2(bw + 20, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##title", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor(2);
    ImGui::PushFont(fonts.bold, 17.0f);
    auto saves = listSaves();
    bool hasAuto = false;
    std::string autoPath;
    for (auto& s : saves)
        if (s.name == "Autosave") { hasAuto = true; autoPath = s.path; }
    if (!saves.empty()) {
        std::string last = saves.front().path;
        if (accentButton(("CONTINUE  -  " + upper(saves.front().name)).c_str(), ImVec2(bw, bh))) loadGame(last);
        ImGui::Spacing();
    }
    if (saves.empty() ? accentButton("NEW CORPORATION", ImVec2(bw, bh)) : ImGui::Button("NEW CORPORATION", ImVec2(bw, bh)))
        showNewGame = true;
    ImGui::Spacing();
    ImGui::BeginDisabled(saves.empty());
    if (ImGui::Button("LOAD GAME", ImVec2(bw, bh))) showLoad = true;
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (ImGui::Button("SETTINGS", ImVec2(bw, bh))) showSettings = true;
    ImGui::Spacing();
    if (ImGui::Button("QUIT", ImVec2(bw, bh))) {
        if (settings.confirmQuit) showQuitConfirm = true; else quit = true;
    }
    ImGui::PopFont();
    (void)hasAuto;
    ImGui::End();

    // footer
    char b[128];
    std::snprintf(b, sizeof b, "v%s  ·  single player sandbox  ·  charts of 80 to 20,000 systems  ·  multiplayer planned", NEBULA_VERSION);
    dl->AddText(fonts.mono, 13.0f * settings.uiScale, ImVec2(P.x + 16, P.y + S.y - 26), withAlpha(col::Dim, 0.85f), b);
}

}  // namespace nb
