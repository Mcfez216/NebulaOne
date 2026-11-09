// NebulaOne - Ship Information: what a hull can do, what is fitted to it, and
// its service record. Fitting happens here while the ship is docked.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "app/widgets.h"

namespace nb {

static std::string shipLabel(const World& w, Id s) {
    const Ship& sh = w.ships[s];
    return (sh.name.empty() ? shipDef(sh.def).name + " #" + std::to_string(s) : sh.name) + "  (" + shipDef(sh.def).cls + ")";
}

void App::winShip() {
    if (!beginWindow(*this, W_SHIP, ImVec2(160, 90), ImVec2(760, 640))) { ImGui::End(); return; }
    World& w = world;
    const Corp& me = w.pc();
    if (selShip != NONE && (selShip >= (Id)w.ships.size() || !w.ships[selShip].alive || w.ships[selShip].corp != w.player)) selShip = NONE;
    if (selShip == NONE)
        for (Id sid : me.shipList)
            if (w.ships[sid].alive && w.ships[sid].corp == w.player) { selShip = sid; break; }
    if (selShip == NONE) { textDim("You own no ships."); ImGui::End(); return; }

    // ---- picker
    ImGui::SetNextItemWidth(360);
    if (ImGui::BeginCombo("##shipsel", shipLabel(w, selShip).c_str(), ImGuiComboFlags_HeightLarge)) {
        for (Id sid : me.shipList) {
            if (!w.ships[sid].alive || w.ships[sid].corp != w.player) continue;
            ImGui::PushID(sid);
            if (ImGui::Selectable(shipLabel(w, sid).c_str(), selShip == sid)) selShip = sid;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    Ship& s = w.ships[selShip];
    const ShipDef& d = shipDef(s.def);
    Id station = s.fleet == NONE ? s.station : w.fleets[s.fleet].docked;
    ImGui::SameLine();
    static char nameBuf[48];
    if (ImGui::SmallButton("Rename")) {
        std::snprintf(nameBuf, sizeof nameBuf, "%s", s.name.c_str());
        ImGui::OpenPopup("rename_ship");
    }
    if (ImGui::BeginPopup("rename_ship")) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("##nm", nameBuf, sizeof nameBuf);
        if (ImGui::Button("OK")) { Command c; c.type = Cmd::RenameShip; c.a = selShip; c.text = nameBuf; run(c, false); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // ---- header
    ImGui::PushFont(fonts.title, 23.0f);
    ImGui::TextUnformatted(s.name.empty() ? d.name.c_str() : s.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    textColored(col::Accent, "%s", upper(d.cls).c_str());
    ImGui::SameLine();
    textDim("%s hull  ·  commissioned %s", d.name.c_str(), fmtDate(s.built).substr(0, 10).c_str());
    ImGui::PushTextWrapPos(0);
    textDim("%s", d.blurb.c_str());
    ImGui::PopTextWrapPos();
    if (s.fleet != NONE) {
        const Fleet& f = w.fleets[s.fleet];
        textColored(col::Accent, "In %s", f.name.c_str());
        ImGui::SameLine();
        textDim("·  %s  ·  %s", w.fleetLocation(f).c_str(), orderKindName(f.order.kind));
    } else {
        ImGui::Text("Docked at %s", w.stations[s.station].name.c_str());
    }
    ImGui::SameLine();
    textDim("·  pilot: %s", s.pilot == NONE ? "none" : w.people[s.pilot].name.c_str());

    // ---- stats, base and fitted
    ShipStats now = w.stats(s);
    Ship bare = s;
    bare.fit = {-1, -1, -1, -1};
    ShipStats base = w.stats(bare);
    header("Performance");
    if (ImGui::BeginTable("perf", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Hull", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("As fitted", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        auto row = [&](const char* label, double b, double n, const char* unit, bool lowerBetter = false, int dec = 0) {
            if (b == 0 && n == 0) return;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
            char bb[32], nb2[32];
            std::snprintf(bb, sizeof bb, dec ? "%.1f %s" : "%.0f %s", b, unit);
            std::snprintf(nb2, sizeof nb2, dec ? "%.1f %s" : "%.0f %s", n, unit);
            ImGui::TableNextColumn(); rightText(bb, col::Dim);
            ImGui::TableNextColumn(); rightText(nb2);
            ImGui::TableNextColumn();
            if (std::fabs(n - b) > 1e-6 && b != 0) {
                double pct = (n / b - 1.0) * 100.0;
                bool good = lowerBetter ? pct < 0 : pct > 0;
                char pb[24];
                std::snprintf(pb, sizeof pb, "%+.0f%%", pct);
                rightText(pb, good ? col::Good : col::Bad);
            } else if (b == 0 && n > 0) {
                rightText("new", col::Good);
            } else {
                rightText("-", col::Dim);
            }
        };
        row("Cargo hold", base.cargo, now.cargo, "m3");
        row("Ore hold", base.oreHold, now.oreHold, "m3");
        row("Mining rate", base.mining, now.mining, "u/h");
        row("Weapons", base.attack, now.attack, "/rnd", false, 1);
        row("Hull", base.hull, now.hull, "hp");
        row("Travel", base.minutesPerHex, now.minutesPerHex, "min/hex", true, 1);
        row("Evasion", base.evasion * 100, now.evasion * 100, "%");
        row("Survey array", base.scan, now.scan, "", false, 2);
        ImGui::EndTable();
    }
    float hf = (float)(s.hull / std::max(1.0, now.hull));
    ImGui::Text("Condition");
    ImGui::SameLine(110);
    progressBar(hf, ImVec2(200, 0), hf < 0.4f ? col::Bad : col::Good);
    ImGui::SameLine();
    textDim("%.0f / %.0f hp  ·  repaired free when docked with its fleet", s.hull, now.hull);

    // ---- fitting
    header(("Fitting  " + std::to_string(d.slots) + " slots").c_str());
    if (station == NONE) textColored(col::Warn, "The ship is in space. Bring it home to change its fittings.");
    for (int k = 0; k < d.slots; k++) {
        ImGui::PushID(k);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float wslot = ImGui::GetContentRegionAvail().x, hslot = ImGui::GetFrameHeight() * 1.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(p, ImVec2(p.x + wslot, p.y + hslot), s.fit[k] >= 0 ? col::Accent : withAlpha(col::Text, 0.15f), 2);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 8, p.y + (hslot - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushFont(fonts.monoBold, 13.0f);
        textDim("SLOT %d", k + 1);
        ImGui::PopFont();
        ImGui::SameLine(80);
        if (s.fit[k] >= 0) {
            int mi = MODULE_FIRST + s.fit[k];
            textColored(col::Accent, "%s", item(mi).name.c_str());
            ImGui::SameLine();
            textDim("%s", moduleDef(s.fit[k]).effect.c_str());
            ImGui::SameLine(wslot - 90);
            ImGui::BeginDisabled(station == NONE);
            if (ImGui::SmallButton("Remove")) { Command c; c.type = Cmd::UnfitModule; c.a = selShip; c.b = k; run(c); }
            ImGui::EndDisabled();
        } else {
            textDim("empty");
            ImGui::SameLine(wslot - 250);
            ImGui::SetNextItemWidth(240);
            ImGui::BeginDisabled(station == NONE);
            if (ImGui::BeginCombo("##fit", "Fit a module...")) {
                bool any = false;
                for (int m = MODULE_FIRST; m < SHIP_FIRST; m++) {
                    int64_t n = station != NONE ? w.have(w.player, station, m) : 0;
                    if (n <= 0) continue;
                    any = true;
                    ImGui::PushID(m);
                    if (ImGui::Selectable((item(m).name + "  x" + fmtInt(n)).c_str())) {
                        Command c; c.type = Cmd::FitModule; c.a = selShip; c.b = k; c.item = m; run(c);
                    }
                    ImGui::SameLine();
                    textDim("%s", moduleDef(item(m).module).effect.c_str());
                    ImGui::PopID();
                }
                if (!any) textDim("No modules in the hangar at %s.", station != NONE ? w.stations[station].name.c_str() : "-");
                if (ImGui::Selectable("Buy modules at this market...")) showMarket(station, MOD_HOLD, 0);
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hslot + 4));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }

    // ---- record
    header("Service record");
    if (ImGui::BeginTable("rec", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); kpi("Value", fmtMoneyShort(w.shipValue(s)));
        ImGui::TableNextColumn(); kpi("Jumps", fmtInt(s.trips));
        ImGui::TableNextColumn(); kpi(d.role == ShipRole::Mining ? "Ore mined" : "Hauled m3", fmtQty(d.role == ShipRole::Mining ? s.mined : s.hauled));
        ImGui::TableNextColumn(); kpi("Kills", fmtInt(s.kills), s.kills ? col::Accent : col::Text);
        ImGui::EndTable();
    }
    bool ins = s.insured && s.insuredUntil > w.now;
    Money prem = w.insurancePremium(s);
    if (ins) textColored(col::Good, "Hull insurance: %s left; pays %s if lost.", fmtDuration(s.insuredUntil - w.now).c_str(),
                         fmtMoneyShort(w.shipValue(s) * 70 / 100).c_str());
    else {
        textDim("Uninsured. Cover for 30 days costs %s, priced on the riskiest rating where it works.", fmtMoney(prem).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Insure")) { Command c; c.type = Cmd::SetInsurance; c.a = selShip; c.flag = true; run(c); }
    }

    // ---- catalogue
    if (ImGui::CollapsingHeader("Module catalogue")) {
        if (beginTable("mods", 4, 0, ImVec2(0, 190))) {
            ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Price here");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            Id mkt = station != NONE ? station : me.home;
            for (int m = MODULE_FIRST; m < SHIP_FIRST; m++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(item(m).name.c_str());
                ImGui::TableNextColumn(); textDim("%s", moduleDef(item(m).module).effect.c_str());
                ImGui::TableNextColumn(); cellMoney(w.refPrice(mkt, m));
                ImGui::TableNextColumn();
                ImGui::PushID(m);
                if (ImGui::SmallButton("Market")) showMarket(mkt, m, 0);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        textDim("Modules stack by multiplying. Military stations and trade hubs sell them; their blueprints are at the Charter hubs.");
    }
    ImGui::End();
}

}  // namespace nb
