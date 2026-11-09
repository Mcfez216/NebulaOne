// NebulaOne - the deep-space backdrop behind the chart: a gas nebula and two
// layers of stars, generated once at start-up as tileable textures and drawn
// with a little parallax as the chart pans. Detached windows get it too, so
// the glass panels look the same on every monitor.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "app/app.h"
#include "app/theme.h"

namespace nb {

ImTextureID createTexture(const void* rgba, int w, int h, bool repeat = false);

namespace {

uint32_t hash3(int x, int y, int s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, int s) { return (hash3(x, y, s) & 0xFFFF) / 65535.0f; }

// Tileable value noise: the lattice wraps every `period` cells.
float noise(float x, float y, int s, int period) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto H = [&](int a, int b) { return rnd(((a % period) + period) % period, ((b % period) + period) % period, s); };
    float a = H(x0, y0) + (H(x0 + 1, y0) - H(x0, y0)) * fx;
    float b = H(x0, y0 + 1) + (H(x0 + 1, y0 + 1) - H(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

float fbm(float u, float v, int s, int base, int octaves) {
    float r = 0, amp = 0.5f, norm = 0;
    int period = base;
    for (int o = 0; o < octaves; o++) {
        r += amp * noise(u * period, v * period, s + o * 17, period);
        norm += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return r / norm;
}

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

ImTextureID makeNebula() {
    const int N = 512;  // soft enough that it is stretched over 1700 px
    std::vector<unsigned char> px((size_t)N * N * 4);
    // three gases: indigo, a cold teal and a warm rust, with dark dust lanes through them
    const float c1[3] = {84, 64, 150}, c2[3] = {34, 112, 126}, c3[3] = {146, 72, 50};
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            // domain warp for wispy filaments
            float wu = u + 0.08f * (fbm(u, v, 5, 3, 4) - 0.5f), wv = v + 0.08f * (fbm(u, v, 9, 3, 4) - 0.5f);
            float d = fbm(wu, wv, 11, 2, 7);
            float t = fbm(u, v, 37, 2, 4), q = fbm(u, v, 71, 3, 4);
            float dust = fbm(wu, wv, 91, 4, 6);
            float dens = smooth(0.44f, 0.80f, d);
            dens = std::pow(dens, 1.5f) * (1.0f - 0.75f * smooth(0.52f, 0.70f, dust));
            float a = smooth(0.3f, 0.7f, t), b = smooth(0.45f, 0.75f, q);
            float r = c1[0] + (c2[0] - c1[0]) * a, g = c1[1] + (c2[1] - c1[1]) * a, bl = c1[2] + (c2[2] - c1[2]) * a;
            r += (c3[0] - r) * b * 0.8f;
            g += (c3[1] - g) * b * 0.8f;
            bl += (c3[2] - bl) * b * 0.8f;
            // brighter cores where the gas is thickest
            float core = smooth(0.70f, 0.95f, d);
            r += 60 * core; g += 50 * core; bl += 40 * core;
            unsigned char* p = &px[((size_t)y * N + x) * 4];
            p[0] = (unsigned char)std::min(255.f, r);
            p[1] = (unsigned char)std::min(255.f, g);
            p[2] = (unsigned char)std::min(255.f, bl);
            p[3] = (unsigned char)std::min(255.f, dens * 120.0f);
        }
    return createTexture(px.data(), N, N, false);
}

ImTextureID makeStars(int seed, int count, float bright) {
    const int N = 1024;
    std::vector<float> lum((size_t)N * N * 3, 0.0f);
    auto put = [&](int x, int y, float r, float g, float b) {
        x = ((x % N) + N) % N;
        y = ((y % N) + N) % N;
        float* p = &lum[((size_t)y * N + x) * 3];
        p[0] += r; p[1] += g; p[2] += b;
    };
    for (int i = 0; i < count; i++) {
        float fx = rnd(i, 1, seed) * N, fy = rnd(i, 2, seed) * N;
        float m = rnd(i, 3, seed);
        float b = bright * (0.25f + 0.75f * m * m * m);
        float tint = rnd(i, 4, seed);
        float r = tint < 0.2f ? 0.75f : tint > 0.85f ? 1.0f : 0.92f;
        float g = tint < 0.2f ? 0.85f : tint > 0.85f ? 0.88f : 0.94f;
        float bl = tint < 0.2f ? 1.0f : tint > 0.85f ? 0.72f : 1.0f;
        int x0 = (int)fx, y0 = (int)fy;
        float ox = fx - x0, oy = fy - y0;
        // a sub-pixel star spread over four pixels, with a soft halo on the bright ones
        float w[4] = {(1 - ox) * (1 - oy), ox * (1 - oy), (1 - ox) * oy, ox * oy};
        put(x0, y0, r * b * w[0], g * b * w[0], bl * b * w[0]);
        put(x0 + 1, y0, r * b * w[1], g * b * w[1], bl * b * w[1]);
        put(x0, y0 + 1, r * b * w[2], g * b * w[2], bl * b * w[2]);
        put(x0 + 1, y0 + 1, r * b * w[3], g * b * w[3], bl * b * w[3]);
        if (m > 0.93f) {
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    float d2 = (float)(dx * dx + dy * dy);
                    if (d2 == 0) continue;
                    float h = b * 0.22f * std::exp(-d2 / 2.2f);
                    put(x0 + dx, y0 + dy, r * h, g * h, bl * h);
                }
        }
    }
    std::vector<unsigned char> px((size_t)N * N * 4);
    for (size_t i = 0; i < (size_t)N * N; i++) {
        float r = lum[i * 3], g = lum[i * 3 + 1], b = lum[i * 3 + 2];
        float a = std::min(1.0f, std::max(r, std::max(g, b)));
        unsigned char* p = &px[i * 4];
        if (a <= 0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
        p[0] = (unsigned char)std::min(255.f, r / a * 255.f);
        p[1] = (unsigned char)std::min(255.f, g / a * 255.f);
        p[2] = (unsigned char)std::min(255.f, b / a * 255.f);
        p[3] = (unsigned char)(a * 255.f);
    }
    return createTexture(px.data(), N, N, false);
}

}  // namespace

void makeBackdrop(Textures& t) {
    t.nebula = makeNebula();
    t.starsFar = makeStars(3, 2600, 0.55f);
    t.starsNear = makeStars(7, 420, 1.0f);
}

// Fills `vp` with deep space. `drift` is the chart's pan in world units, so the
// layers slide a little as you scroll, slower the further away they are.
void App::drawBackdrop(ImGuiViewport* vp, ImVec2 drift) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    ImVec2 P = vp->Pos, S = vp->Size, Q(P.x + S.x, P.y + S.y);
    dl->AddRectFilledMultiColor(P, Q, IM_COL32(12, 15, 23, 255), IM_COL32(10, 12, 19, 255), IM_COL32(6, 7, 11, 255),
                                IM_COL32(7, 8, 12, 255));
    // detached windows use their position on the desktop so the sky lines up across monitors
    // The renderer samples with clamp-to-edge, so tiles are laid out by hand rather than with wrapping UVs.
    auto layer = [&](ImTextureID tx, float texPx, float k, ImU32 tint) {
        if (!tx) return;
        float ox = std::fmod(P.x + drift.x * k, texPx), oy = std::fmod(P.y + drift.y * k, texPx);
        if (ox < 0) ox += texPx;
        if (oy < 0) oy += texPx;
        for (float y = P.y - oy; y < Q.y; y += texPx)
            for (float x = P.x - ox; x < Q.x; x += texPx) dl->AddImage(ImTextureRef(tx), ImVec2(x, y), ImVec2(x + texPx, y + texPx), ImVec2(0, 0), ImVec2(1, 1), tint);
    };
    layer(tex.nebula, 1700.0f, 0.35f, IM_COL32(255, 255, 255, 235));
    layer(tex.nebula, 900.0f, 0.55f, IM_COL32(255, 255, 255, 70));
    layer(tex.starsFar, 1024.0f, 0.15f, IM_COL32(255, 255, 255, 200));
    layer(tex.starsNear, 1024.0f, 0.6f, IM_COL32(255, 255, 255, 255));
    // a soft vignette keeps the edges of the screen quiet
    float e = std::min(S.x, S.y) * 0.22f;
    ImU32 dark = IM_COL32(3, 4, 7, 150), clear = IM_COL32(3, 4, 7, 0);
    dl->AddRectFilledMultiColor(P, ImVec2(Q.x, P.y + e), dark, dark, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(P.x, Q.y - e), Q, clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(P, ImVec2(P.x + e, Q.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(Q.x - e, P.y), Q, clear, dark, dark, clear);
}

}  // namespace nb
