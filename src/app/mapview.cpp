// NebulaOne - the real-time view: a hex sector chart behind every window.
//
// Flat-topped hexes, wargame style. The chart is culled to the screen and drops
// detail as you zoom out, so even a 20,000-system Expanse draws in a frame.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "app/app.h"
#include "imgui_internal.h"
#include "app/theme.h"
#include "app/widgets.h"

namespace nb {

float uiTopH();
float uiKeysH();
float uiTickerH();

static constexpr float HEX = 10.0f;
static constexpr ImU32 kBorderLane = IM_COL32(236, 206, 150, 96);  // world units, centre to corner (matches the core's hex layout)

static ImU32 corpColor(Id corp, int alpha) {
    uint32_t h = (uint32_t)corp * 2654435761u;
    float hue = (h % 360) / 360.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}

static ImU32 fleetColor(const World& w, const Fleet& f) {
    if (f.corp == w.player) return col::Accent;
    const Corp& c = w.corps[f.corp];
    if (w.atWar(f.corp, w.player)) return col::Bad;
    if (w.allied(f.corp, w.player)) return col::Good;
    if (c.arch == Arch::Pirate || c.arch == Arch::Ganker || w.standing(f.corp, w.player) <= -5 || w.isOutlaw(f.corp)) return col::Bad;
    return IM_COL32(170, 164, 150, 210);
}

// Region borders: the cell edges where the region changes, worked out once per chart.
struct BorderCache {
    uint64_t seed = 0;
    size_t cells = 0;
    std::vector<std::pair<ImVec2, ImVec2>> segs;  // world coordinates
};

static void hexCorners(float cx, float cy, float size, ImVec2 out[6]) {
    for (int i = 0; i < 6; i++) {
        float a = 3.14159265f / 3.0f * i;
        out[i] = ImVec2(cx + size * std::cos(a), cy + size * std::sin(a));
    }
}

static const BorderCache& borders(const World& w) {
    static BorderCache bc;
    if (bc.seed == w.seed && bc.cells == w.cells.size()) return bc;
    bc.seed = w.seed;
    bc.cells = w.cells.size();
    bc.segs.clear();
    // flat-top neighbours and the corner pair that forms the shared edge
    static const int dir[6][2] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    static const int edge[6][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}};
    for (auto& c : w.cells) {
        float cx = 15.0f * c.q, cy = 10.0f * 1.7320508f * (c.r + c.q * 0.5f);
        ImVec2 cr[6];
        hexCorners(cx, cy, HEX, cr);
        for (int d = 0; d < 6; d++) {
            int n = w.cellAt(c.q + dir[d][0], c.r + dir[d][1]);
            if (n < 0 || w.cells[n].region == c.region) continue;
            if (n < (int)(&c - &w.cells[0])) continue;  // each edge once
            bc.segs.push_back({cr[edge[d][0]], cr[edge[d][1]]});
        }
    }
    return bc;
}

void App::drawMap() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    ImVec2 P = vp->Pos, S = vp->Size;
    if (!inGame) {
        drawBackdrop(vp, ImVec2((float)time * 9.0f, (float)time * 3.0f));
        // title backdrop: a slow-drifting hex lattice
        float sz = 34.0f * settings.uiScale;
        float ox = std::fmod((float)time * 6.0f, sz * 3.0f), oy = std::fmod((float)time * 3.0f, sz * 1.7320508f);
        for (float x = -sz * 3; x < S.x + sz * 3; x += sz * 1.5f) {
            int col_ = (int)std::floor(x / (sz * 1.5f));
            for (float y = -sz * 2; y < S.y + sz * 2; y += sz * 1.7320508f) {
                float yy = y + ((col_ & 1) ? sz * 0.866f : 0);
                ImVec2 c[6];
                hexCorners(P.x + x - ox, P.y + yy - oy, sz * 0.97f, c);
                dl->AddPolyline(c, 6, withAlpha(col::Accent, 0.07f), ImDrawFlags_Closed, 1.0f);
            }
        }
        return;
    }

    World& w = world;
    float scale = ImGui::GetStyle().FontScaleMain;
    float top = uiTopH() + uiKeysH(), bottom = uiTickerH();
    ImVec2 A(P.x, P.y + top), B(P.x + S.x, P.y + S.y - bottom);
    ImVec2 C((A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f);
    float extentX = 15.0f * (w.hexRadius * 2 + 2), extentY = 17.32f * (w.hexRadius * 2 + 2);
    float fit = std::min((B.x - A.x) / extentX, (B.y - A.y) / extentY) * 1.05f;
    if (zoom < 0) { zoom = fit; pan = ImVec2(0, 0); }
    if (zoom == 0) {
        // open on home, zoomed so a handful of jumps around it fill the screen
        zoom = std::max(fit, std::min((B.x - A.x) / 520.0f, (B.y - A.y) / 420.0f));
        centerMap(w.stations[w.pc().home].system);
    }
    drawBackdrop(vp, ImVec2(pan.x * 2.0f, pan.y * 2.0f));
    auto toScreen = [&](float x, float y) { return ImVec2(C.x + (x - pan.x) * zoom, C.y + (y - pan.y) * zoom); };
    float hexPx = HEX * zoom;  // a hex's radius on screen
    ImFont* font = fonts.mono ? fonts.mono : ImGui::GetFont();
    float fs = 13.0f * scale;
    // visible world rectangle, with margin
    float wx0 = pan.x + (A.x - C.x) / zoom - 30, wx1 = pan.x + (B.x - C.x) / zoom + 30;
    float wy0 = pan.y + (A.y - C.y) / zoom - 30, wy1 = pan.y + (B.y - C.y) / zoom + 30;
    auto visible = [&](float x, float y) { return x >= wx0 && x <= wx1 && y >= wy0 && y <= wy1; };
    dl->PushClipRect(A, B, true);

    // ---- cells: zone wash and the grid, only once hexes are big enough to read
    if (hexPx >= 4.0f) {
        for (auto& c : w.cells) {
            float cx = 15.0f * c.q, cy = 17.320508f * (c.r + c.q * 0.5f);
            if (!visible(cx, cy)) continue;
            ImVec2 sp = toScreen(cx, cy);
            ImVec2 cr[6];
            hexCorners(sp.x, sp.y, hexPx * 0.985f, cr);
            ImU32 fill = 0;
            switch (mapMode) {
                case MapMode::Zones: fill = zoneColor(c.zone, c.zone == Zone::Charter ? 16 : c.zone == Zone::Contract ? 14 : 12); break;
                case MapMode::Risk:
                    if (c.system != NONE) fill = riskColor(w.systems[c.system].risk, 34);
                    break;
                case MapMode::Sovereignty:
                    if (c.system != NONE && w.systems[c.system].owner != NONE) fill = corpColor(w.systems[c.system].owner, 60);
                    break;
                case MapMode::Regions:
                    if (c.region != NONE) fill = corpColor(c.region * 7 + 3, 18);
                    break;
                case MapMode::Intel:
                case MapMode::Events:
                    fill = zoneColor(c.zone, 7);
                    break;
            }
            if (fill) dl->AddConvexPolyFilled(cr, 6, fill);
            if (settings.hexGrid && hexPx >= 9.0f) dl->AddPolyline(cr, 6, IM_COL32(242, 225, 190, hexPx > 30 ? 20 : 12), ImDrawFlags_Closed, 1.0f);
        }
    }
    // region borders and names
    if (mapMode == MapMode::Regions || hexPx < 30.0f) {
        if (hexPx >= 2.0f) {
            const BorderCache& bc = borders(w);
            ImU32 bcol = IM_COL32(242, 225, 190, mapMode == MapMode::Regions ? 70 : 26);
            for (auto& sg : bc.segs) {
                if (!visible(sg.first.x, sg.first.y)) continue;
                dl->AddLine(toScreen(sg.first.x, sg.first.y), toScreen(sg.second.x, sg.second.y), bcol, 1.0f);
            }
        }
        for (auto& r : w.regions) {
            if (hexPx < 2.5f) break;  // too far out: the names would pile up
            if (!visible(r.cx, r.cy)) continue;
            ImVec2 p = toScreen(r.cx, r.cy);
            std::string t = upper(r.name);
            float sz = fs * 1.05f;
            ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0, t.c_str());
            dl->AddText(font, sz, ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), IM_COL32(232, 225, 208, hexPx < 6 ? 70 : 38), t.c_str());
        }
    }

    // ---- lanes
    float laneW = hexPx > 20 ? 1.6f : 1.0f;
    for (auto& s : w.systems) {
        bool sv = visible(s.x, s.y);
        for (auto& l : s.lanes) {
            if (l.to < s.id) continue;
            const StarSystem& t = w.systems[l.to];
            if (!sv && !visible(t.x, t.y)) continue;
            if (w.isGate(s.id, l.to)) {  // jump gates: a bright double line
                ImVec2 a = toScreen(s.x, s.y), b = toScreen(t.x, t.y);
                float dx = b.x - a.x, dy = b.y - a.y, len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
                ImVec2 nrm(-dy / len * 2.0f, dx / len * 2.0f);
                ImU32 gc = IM_COL32(150, 220, 255, 200);
                dl->AddLine(ImVec2(a.x + nrm.x, a.y + nrm.y), ImVec2(b.x + nrm.x, b.y + nrm.y), gc, 1.4f);
                dl->AddLine(ImVec2(a.x - nrm.x, a.y - nrm.y), ImVec2(b.x - nrm.x, b.y - nrm.y), gc, 1.4f);
                continue;
            }
            ImU32 lc;
            if (mapMode == MapMode::Risk) lc = riskColor(std::max(s.risk, t.risk), 120);
            else if (s.zone != t.zone) lc = kBorderLane;  // border crossings stand out: the chokepoints
            else lc = IM_COL32(232, 225, 208, 52);
            dl->AddLine(toScreen(s.x, s.y), toScreen(t.x, t.y), lc, laneW);
        }
    }
    // your drift lanes: dashed shortcuts
    for (auto& e : w.drift) {
        if (e.corp != w.player) continue;
        ImVec2 p1 = toScreen(w.systems[e.a].x, w.systems[e.a].y), p2 = toScreen(w.systems[e.b].x, w.systems[e.b].y);
        float len = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
        int dashes = std::max(2, (int)(len / 10));
        for (int k = 0; k < dashes; k += 2) {
            float t0 = (float)k / dashes, t1 = (float)(k + 1) / dashes;
            dl->AddLine(ImVec2(p1.x + (p2.x - p1.x) * t0, p1.y + (p2.y - p1.y) * t0), ImVec2(p1.x + (p2.x - p1.x) * t1, p1.y + (p2.y - p1.y) * t1),
                        withAlpha(col::Accent, 0.8f), 1.6f);
        }
    }
    // sensor coverage
    if (mapMode == MapMode::Intel && w.fog) {
        for (auto& s : w.systems) {
            if (!visible(s.x, s.y) || w.sight[s.id] == 0) continue;
            ImVec2 p = toScreen(s.x, s.y);
            ImVec2 cr[6];
            hexCorners(p.x, p.y, hexPx * 1.6f, cr);
            dl->AddConvexPolyFilled(cr, 6, w.sight[s.id] >= 2 ? withAlpha(col::Accent, 0.16f) : withAlpha(col::Info, 0.12f));
        }
    }
    // selected fleet's route
    if (selFleet != NONE && selFleet < (Id)w.fleets.size() && w.fleets[selFleet].alive) {
        const Fleet& f = w.fleets[selFleet];
        Id prev = f.system;
        std::vector<Id> path = f.path;
        if (f.act == Act::Jump && f.jumpTo != NONE && (path.empty() || path.front() != f.jumpTo)) path.insert(path.begin(), f.jumpTo);
        for (Id s : path) {
            dl->AddLine(toScreen(w.systems[prev].x, w.systems[prev].y), toScreen(w.systems[s].x, w.systems[s].y), col::Accent, 2.5f);
            prev = s;
        }
    }

    // ---- systems
    Id homeSys = w.stations[w.pc().home].system;
    float r0 = std::clamp(hexPx * 0.16f, 1.2f, 6.0f);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    for (auto& s : w.systems) {
        if (!visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        ImU32 c = mapMode == MapMode::Risk ? riskColor(s.risk) : zoneColor(s.zone);
        if (mapMode == MapMode::Sovereignty && s.owner != NONE) c = corpColor(s.owner, 255);
        if (r0 < 2.0f) dl->AddRectFilled(ImVec2(p.x - r0, p.y - r0), ImVec2(p.x + r0, p.y + r0), c);
        else dl->AddCircleFilled(p, r0, c, 10);
        bool hub = false, outpost = false;
        Id outOwner = NONE;
        for (Id st : s.stations) {
            const Station& S2 = w.stations[st];
            if (S2.dead) continue;
            hub = hub || S2.type == StationType::TradeHub;
            if (S2.isOutpost()) { outpost = true; outOwner = S2.owner; }
        }
        if (hexPx >= 6.0f) {
            if (!s.stations.empty() && !outpost) dl->AddRect(ImVec2(p.x - r0 - 2.5f, p.y - r0 - 2.5f), ImVec2(p.x + r0 + 2.5f, p.y + r0 + 2.5f), withAlpha(c, 0.6f));
            if (hub) {
                float d = r0 + 5.5f;
                dl->AddRect(ImVec2(p.x - d, p.y - d), ImVec2(p.x + d, p.y + d), col::Accent, 0, 0, 1.4f);
            }
            if (outpost) {
                float d = r0 + 6.0f;
                ImU32 oc = outOwner == w.player ? col::Accent : corpColor(outOwner, 230);
                dl->AddTriangle(ImVec2(p.x, p.y - d), ImVec2(p.x + d * 0.9f, p.y + d * 0.6f), ImVec2(p.x - d * 0.9f, p.y + d * 0.6f), oc, 1.6f);
            }
        }
        if (s.id == homeSys) dl->AddCircle(p, r0 + 9.0f, withAlpha(col::Accent, 0.7f), 24, 1.2f);
        if (s.id == selSystem) {
            float pr = r0 + 11.0f + 1.5f * std::sin((float)time * 3.0f);
            ImVec2 cr[6];
            hexCorners(p.x, p.y, pr, cr);
            dl->AddPolyline(cr, 6, col::Accent, ImDrawFlags_Closed, 1.8f);
        }
        bool label = (hub && hexPx >= 7.0f) || s.id == homeSys || s.id == selSystem || (settings.mapLabels && hexPx >= 20.0f);
        if (label) {
            float sz = fs * (hub ? 1.0f : 0.92f);
            dl->AddText(font, sz, ImVec2(p.x + r0 + 6, p.y - sz * 0.5f - 1), hub ? col::Text : IM_COL32(205, 198, 182, 200), s.name.c_str());
            if (hexPx >= 32.0f) {
                std::string sub = s.hexId + "  " + riskGrade(s.risk);
                dl->AddText(font, sz * 0.82f, ImVec2(p.x + r0 + 6, p.y + sz * 0.45f), withAlpha(riskColor(s.risk), 0.8f), sub.c_str());
            }
        }
    }
    // ---- battles
    for (auto& b : w.battles) {
        if (!b.active) continue;
        if (!w.sighted(b.system) && b.attCorp != w.player && b.defCorp != w.player) continue;
        const StarSystem& s = w.systems[b.system];
        if (!visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        float pr = r0 + 8 + 4 * std::sin((float)time * 7);
        dl->AddCircle(p, pr, withAlpha(col::Bad, 0.9f), 20, 2.0f);
        dl->AddLine(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), col::Bad, 1.5f);
        dl->AddLine(ImVec2(p.x - 4, p.y + 4), ImVec2(p.x + 4, p.y - 4), col::Bad, 1.5f);
    }
    // ---- fleets
    std::unordered_map<Id, int> slot;
    for (auto& f : w.fleets) {
        if (!f.alive || f.ships.empty()) continue;
        const StarSystem& s = w.systems[f.system];
        bool mine = f.corp == w.player;
        if (!mine && hexPx < 5.0f) continue;  // too far out to tell fleets apart
        if (!mine && !w.sighted(f.system) && !(f.act == Act::Jump && f.jumpTo != NONE && w.sighted(f.jumpTo))) continue;  // fog of war
        ImVec2 p;
        float dirx = 0, diry = -1;
        if (f.act == Act::Jump && f.jumpTo != NONE) {
            const StarSystem& t = w.systems[f.jumpTo];
            if (!visible(s.x, s.y) && !visible(t.x, t.y)) continue;
            float span = (float)std::max<Tick>(1, f.actEnd - f.actStart);
            float k = std::clamp(((float)(w.now - f.actStart) + (float)frac) / span, 0.0f, 1.0f);
            p = toScreen(s.x + (t.x - s.x) * k, s.y + (t.y - s.y) * k);
            float lx = t.x - s.x, ly = t.y - s.y, ll = std::sqrt(lx * lx + ly * ly);
            if (ll > 0) { dirx = lx / ll; diry = ly / ll; }
        } else {
            if (!visible(s.x, s.y)) continue;
            int k = slot[f.system]++;
            float ang = 0.9f + k * 0.75f, rad = r0 + 9.0f + (k / 8) * 6.0f;
            ImVec2 c = toScreen(s.x, s.y);
            p = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
        }
        ImU32 c = fleetColor(w, f);
        if (mine) {
            float sz = 5.5f;
            float nx = -diry, ny = dirx;
            ImVec2 tip(p.x + dirx * sz * 1.4f, p.y + diry * sz * 1.4f);
            ImVec2 l(p.x - dirx * sz + nx * sz, p.y - diry * sz + ny * sz), rr(p.x - dirx * sz - nx * sz, p.y - diry * sz - ny * sz);
            dl->AddTriangleFilled(tip, l, rr, c);
            if (f.id == selFleet) dl->AddCircle(p, 9, col::Accent, 16, 1.2f);
            if (hexPx > 14.0f || f.id == selFleet) dl->AddText(font, fs * 0.85f, ImVec2(p.x + 8, p.y - 5), col::Accent, f.name.c_str());
        } else {
            dl->AddRectFilled(ImVec2(p.x - 1.8f, p.y - 1.8f), ImVec2(p.x + 1.8f, p.y + 1.8f), c);
        }
    }
    // ---- your sites: small diamonds beside the system
    for (auto& st : w.sites) {
        if (st.finder != w.player || st.done || st.expires <= w.now) continue;
        const StarSystem& s = w.systems[st.system];
        if (!visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        float d = std::max(3.0f, r0 * 0.9f);
        ImVec2 c(p.x - r0 - 7, p.y - r0 - 5);
        ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
        dl->AddConvexPolyFilled(q, 4, st.kind == SiteKind::Drift ? withAlpha(col::Accent, 0.5f) : col::Accent);
    }
    // ---- megaprojects: beacons, survey arrays, gates and works in progress
    for (auto& s : w.systems) {
        if (!(s.beacon == 2 || s.surveyArray != NONE) || !visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        float rr = std::max(7.0f, hexPx * 0.75f);
        if (s.beacon == 2) dl->AddCircle(p, rr, withAlpha(zoneColor(Zone::Charter), 0.9f), 24, 1.6f);
        if (s.surveyArray != NONE) {
            dl->AddCircle(p, rr * 1.25f, withAlpha(s.surveyArray == w.player ? col::Accent : col::Info, 0.7f), 24, 1.0f);
            dl->AddLine(ImVec2(p.x, p.y - rr * 1.25f), ImVec2(p.x, p.y - rr * 1.6f), withAlpha(col::Info, 0.8f), 1.2f);
        }
    }
    for (auto& pj : w.projects) {
        if (pj.stage > ProjectStage::Building || pj.kind == ProjectKind::Upgrade) continue;
        const StarSystem& s = w.systems[pj.system];
        if (!visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        float rr = std::max(6.0f, hexPx * 0.6f);
        float f = (float)pj.progress();
        dl->PathArcTo(p, rr + 3, -1.5708f, -1.5708f + 6.2832f * std::max(0.02f, pj.stage == ProjectStage::Building ? 1.0f : f), 24);
        dl->PathStroke(pj.stage == ProjectStage::Building ? col::Good : col::Warn, 0, 2.0f);
        if (pj.kind == ProjectKind::JumpGate && hexPx > 6) {
            const StarSystem& t = w.systems[pj.target];
            ImVec2 q = toScreen(t.x, t.y);
            int dashes = 24;
            for (int k = 0; k < dashes; k += 2) {
                float t0 = (float)k / dashes, t1 = (float)(k + 1) / dashes;
                dl->AddLine(ImVec2(p.x + (q.x - p.x) * t0, p.y + (q.y - p.y) * t0), ImVec2(p.x + (q.x - p.x) * t1, p.y + (q.y - p.y) * t1),
                            IM_COL32(150, 220, 255, 90), 1.0f);
            }
        }
        if (hexPx > 14) dl->AddText(font, fs * 0.75f, ImVec2(p.x + rr + 6, p.y + rr), withAlpha(col::Warn, 0.9f), projectKindName(pj.kind));
    }
    // ---- storms always show: they close lanes
    for (auto& s : w.systems) {
        if (!w.stormBlocked(s.id) || !visible(s.x, s.y)) continue;
        ImVec2 p = toScreen(s.x, s.y);
        float rr = std::max(8.0f, hexPx * 0.9f);
        for (int k = 0; k < 3; k++) {
            float a0 = (float)time * 1.5f + k * 2.0944f;
            dl->PathArcTo(p, rr, a0, a0 + 1.4f, 12);
            dl->PathStroke(withAlpha(col::Info, 0.85f), 0, 2.0f);
        }
        if (hexPx > 12) dl->AddText(font, fs * 0.8f, ImVec2(p.x - rr, p.y + rr + 2), withAlpha(col::Info, 0.9f), "ION STORM");
    }
    // ---- galaxy events
    if (mapMode == MapMode::Events) {
        for (auto& e : w.events) {
            if (e.over || e.kind == EventKind::Storm) continue;
            ImU32 c = e.kind == EventKind::PirateSurge ? col::Bad : e.kind == EventKind::GoldRush ? col::Good
                      : e.kind == EventKind::Crackdown ? zoneColor(Zone::Charter) : col::Warn;
            if (!e.applied) c = withAlpha(c, 0.55f);
            float x, y;
            if (e.system != NONE) { x = w.systems[e.system].x; y = w.systems[e.system].y; }
            else if (e.region != NONE) { x = w.regions[e.region].cx; y = w.regions[e.region].cy; }
            else continue;
            if (!visible(x, y)) continue;
            ImVec2 p = toScreen(x, y);
            bool regional = e.system == NONE;
            float rr = regional ? std::max(18.0f, hexPx * 3.0f) : std::max(9.0f, hexPx * 0.8f);
            dl->AddCircle(p, rr, c, 32, regional ? 1.4f : 2.0f);
            std::string lab = upper(eventKindName(e.kind));
            if (e.item >= 0 && e.kind != EventKind::GoldRush) lab += " " + upper(item(e.item).name);
            if (!e.applied) lab += " (SOON)";
            float sz = fs * 0.85f;
            ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0, lab.c_str());
            dl->AddText(font, sz, ImVec2(p.x - ts.x * 0.5f, p.y + rr + 2), c, lab.c_str());
        }
    }
    dl->PopClipRect();

    // ---- hover: the hex under the cursor and the nearest system around it
    float mx = pan.x + (mouse.x - C.x) / zoom, my = pan.y + (mouse.y - C.y) / zoom;
    double fq = (2.0 / 3.0 * mx) / HEX, fr = (-1.0 / 3.0 * mx + std::sqrt(3.0) / 3.0 * my) / HEX;
    double fsx = -fq - fr;
    int rq = (int)std::round(fq), rr = (int)std::round(fr), rs = (int)std::round(fsx);
    double dq = std::fabs(rq - fq), dr = std::fabs(rr - fr), ds = std::fabs(rs - fsx);
    if (dq > dr && dq > ds) rq = -rr - rs;
    else if (dr > ds) rr = -rq - rs;
    Id hovered = NONE;
    float bestD = std::max(14.0f * scale, hexPx * 0.9f);
    for (int a = -2; a <= 2; a++)
        for (int b2 = -2; b2 <= 2; b2++) {
            int ci = w.cellAt(rq + a, rr + b2);
            if (ci < 0 || w.cells[ci].system == NONE) continue;
            const StarSystem& s = w.systems[w.cells[ci].system];
            ImVec2 p = toScreen(s.x, s.y);
            float d = std::sqrt((mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y));
            if (d < bestD) { bestD = d; hovered = s.id; }
        }

    // ---- interaction (only when the mouse is over open chart)
    ImGuiIO& io = ImGui::GetIO();
    bool free = !io.WantCaptureMouse && mouse.x > A.x && mouse.y > A.y && mouse.y < B.y;
    static bool dragging = false;
    static ImVec2 dragStart;
    float maxZoom = 140.0f / HEX;
    if (free && io.MouseWheel != 0) {
        float before = zoom;
        zoom = std::clamp(zoom * (io.MouseWheel > 0 ? 1.18f : 1.0f / 1.18f), fit * 0.7f, std::max(fit, maxZoom));
        float wx = pan.x + (mouse.x - C.x) / before, wy = pan.y + (mouse.y - C.y) / before;
        pan.x = wx - (mouse.x - C.x) / zoom;
        pan.y = wy - (mouse.y - C.y) / zoom;
    }
    if (free && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
        dragging = true;
        dragStart = mouse;
    }
    if (dragging) {
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                    ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        pan.x -= io.MouseDelta.x / zoom;
        pan.y -= io.MouseDelta.y / zoom;
        if (!down) {
            float dx = mouse.x - dragStart.x, dy = mouse.y - dragStart.y;
            if (dx * dx + dy * dy < 16 && hovered != NONE) selectSystem(hovered);
            dragging = false;
        }
    }
    if (free && !dragging) {
        int ci = w.cellAt(rq, rr);
        if (hovered != NONE) {
            const StarSystem& s = w.systems[hovered];
            ImGui::BeginTooltip();
            ImGui::PushFont(fonts.bold, kFontHead);
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            monoText(s.hexId, col::Dim);
            zoneBadge(s.zone);
            ImGui::SameLine();
            riskBadge(s.risk, s.riskLive);
            ImGui::SameLine();
            textDim("%s", w.regions[s.region].name.c_str());
            if (s.owner != NONE) textColored(s.owner == w.player ? col::Accent : col::Warn, "Held by %s", w.corps[s.owner].name.c_str());
            int mine = 0, neutral = 0, hostile = 0;
            if (hovered < (Id)w.fleetsIn.size() && w.sighted(hovered))
                for (Id fid : w.fleetsIn[hovered]) {
                    const Fleet& f = w.fleets[fid];
                    if (!f.alive || f.system != hovered) continue;
                    if (f.corp == w.player) mine++;
                    else if (fleetColor(w, f) == col::Bad) hostile++;
                    else neutral++;
                }
            for (Id fid : w.pc().fleetList)
                if (w.fleets[fid].alive && w.fleets[fid].system == hovered && w.fleets[fid].docked != NONE) mine++;
            ImGui::Text("%d stations  ·  %d belts  ·  %d jumps from home", (int)s.stations.size(), (int)s.belts.size(), w.jumps(homeSys, hovered));
            if (mine) textColored(col::Accent, "%d of your fleets here", mine);
            if (hostile) textColored(col::Bad, "%d hostile fleets in space", hostile);
            if (neutral) textDim("%d other fleets in space", neutral);
            if (!w.sighted(hovered)) textDim("No sensors here: other fleets unknown");
            if (w.stormBlocked(hovered)) textColored(col::Warn, "Ion storm: lanes closed for %s", fmtDuration(w.stormUntil[hovered] - w.now).c_str());
            for (auto& st : w.sites)
                if (st.finder == w.player && st.system == hovered && !st.done && st.expires > w.now) textColored(col::Accent, "%s", st.name.c_str());
            textDim("Click for details");
            ImGui::EndTooltip();
        }
        (void)ci;
    }

    // ---- chart controls and legend (bottom left)
    // sits in the corner of the open chart, clear of any docked columns or rows
    ImVec2 keyAt(A.x + 10, B.y - 10);
    if (ImGuiDockNode* cn = dockId ? ImGui::DockBuilderGetCentralNode(dockId) : nullptr)
        if (cn->Size.x > 200 && cn->Size.y > 150) keyAt = ImVec2(cn->Pos.x + 10, cn->Pos.y + cn->Size.y - 10);
    ImGui::SetNextWindowPos(keyAt, ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##chartkey", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::PushFont(fonts.monoBold, 12.5f);
    static const char* modes[] = {"ZONES", "RISK", "SOV", "REGIONS", "INTEL", "EVENTS"};
    for (int m = 0; m < 6; m++) {
        if (m) ImGui::SameLine(0, 2);
        bool on = (int)mapMode == m;
        if (on) { ImGui::PushStyleColor(ImGuiCol_Button, col::Accent); ImGui::PushStyleColor(ImGuiCol_Text, col::OnAccent); }
        if (ImGui::Button(modes[m])) mapMode = (MapMode)m;
        if (on) ImGui::PopStyleColor(2);
    }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("HOME")) centerMap(homeSys);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("FIT")) { zoom = fit; pan = ImVec2(0, 0); }
    ImGui::PopFont();
    ImGui::PushFont(nullptr, kFontSmall);
    ImDrawList* kd = ImGui::GetWindowDrawList();
    auto swatch = [&](ImU32 c, const char* label) {
        ImVec2 q = ImGui::GetCursorScreenPos();
        float hh = ImGui::GetTextLineHeight();
        kd->AddRectFilled(ImVec2(q.x, q.y + hh * 0.25f), ImVec2(q.x + 10, q.y + hh * 0.75f), c);
        ImGui::Dummy(ImVec2(12, hh));
        ImGui::SameLine(0, 4);
        textDim("%s", label);
        ImGui::SameLine(0, 12);
    };
    if (mapMode == MapMode::Intel) {
        swatch(withAlpha(col::Accent, 0.6f), "Live sensors");
        swatch(withAlpha(col::Info, 0.6f), "Broker sightings");
        swatch(col::Accent, "Your sites, drift lanes (dashed)");
    } else if (mapMode == MapMode::Events) {
        swatch(col::Warn, "Markets: shortage, strike, boom");
        swatch(col::Info, "Ion storm");
        swatch(col::Bad, "Raiders");
        swatch(col::Good, "Gold rush");
    } else if (mapMode == MapMode::Risk) {
        static const char* g[RISK_GRADES] = {"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "D"};
        static const float mid[RISK_GRADES] = {5, 15, 25, 36, 48, 61, 75, 90};
        for (int i = 0; i < RISK_GRADES; i++) swatch(riskColor(mid[i]), g[i]);
    } else {
        swatch(zoneColor(Zone::Charter), "Charter: Wardens respond");
        swatch(zoneColor(Zone::Contract), "Contract: no police");
        swatch(zoneColor(Zone::Frontier), "Frontier: outposts only");
    }
    ImGui::NewLine();
    swatch(col::Accent, "Yours / hub");
    swatch(col::Bad, "Hostile / combat");
    swatch(kBorderLane | IM_COL32(0, 0, 0, 255), "Zone-border lane");
    ImGui::NewLine();
    ImGui::PopFont();
    ImVec2 kmin = ImGui::GetWindowPos(), ksz = ImGui::GetWindowSize();
    anchors["map_legend"] = ImVec4(kmin.x, kmin.y, kmin.x + ksz.x, kmin.y + ksz.y);
    ImGui::End();
}

void App::centerMap(Id system) {
    if (system == NONE) return;
    pan = ImVec2(world.systems[system].x, world.systems[system].y);
}

}  // namespace nb
