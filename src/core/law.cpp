// NebulaOne - the law: Charter, Contract and Frontier space, and the
// underwriters' risk ratings that price insurance and steer routes.
//
// A system's rating starts from its zone and depth, then moves with what
// actually happens there: violence (heat), value destroyed (losses) and the
// hours hostile fleets spend in it. Ratings are published weekly; the daily
// figure shows which way the next one is heading (the outlook).
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

static float baseRisk(const StarSystem& s) {
    switch (s.zone) {
        case Zone::Charter: return 8.0f + 3.0f * s.depth;
        case Zone::Contract: return 35.0f + 5.0f * s.depth - (s.beacon == 2 ? 15.0f : s.beacon == 1 ? 8.0f : 0.0f);
        case Zone::Frontier: return 70.0f;
    }
    return 50.0f;
}

void World::riskDaily() {
    std::vector<float> own(systems.size());
    for (size_t i = 0; i < systems.size(); i++) {
        const StarSystem& s = systems[i];
        double v = baseRisk(s) + 12.0 * std::log2(1.0 + s.heat) + 25.0 * std::log10(1.0 + s.losses) +
                   3.0 * std::log2(1.0 + s.hostileHours / 24.0);
        // an online outpost brings order of a kind
        if (s.owner != NONE) v -= 8.0;
        own[i] = (float)std::clamp(v, 0.0, 100.0);
    }
    for (size_t i = 0; i < systems.size(); i++) {
        StarSystem& s = systems[i];
        double nb = 0;
        for (auto& l : s.lanes) nb += own[l.to];
        nb = s.lanes.empty() ? own[i] : nb / s.lanes.size();
        s.riskLive = (float)std::clamp(0.7 * own[i] + 0.3 * nb, 0.0, 100.0);
        s.heat *= 0.85f;
        s.losses *= 0.90f;
        s.hostileHours *= 0.80f;
        if (s.heat < 0.01f) s.heat = 0;
        if (s.losses < 0.001f) s.losses = 0;
    }
}

void World::riskWeekly() {
    struct Move { float delta; Id sys; int from, to; };
    std::vector<Move> moves;
    std::vector<char> mine(systems.size(), 0);
    for (Id f : pc().fleetList)
        if (fleets[f].alive) {
            mine[fleets[f].system] = 1;
            if (fleets[f].order.system != NONE) mine[fleets[f].order.system] = 1;
        }
    for (auto& s : systems) {
        int g0 = riskGradeIndex(s.risk);
        float old = s.risk;
        s.risk = s.riskLive;
        int g1 = riskGradeIndex(s.risk);
        if (g0 != g1 && (!s.stations.empty() || mine[s.id])) moves.push_back({s.risk - old, s.id, g0, g1});
    }
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) { return std::fabs(a.delta) > std::fabs(b.delta); });
    static const char* G[RISK_GRADES] = {"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "D"};
    int shown = 0;
    for (auto& m : moves) {
        bool personal = mine[m.sys];
        if (!personal && shown >= 5) continue;
        const StarSystem& s = systems[m.sys];
        std::string verb = m.to > m.from ? "downgrades" : "upgrades";
        addLog(Chan::News, m.to > m.from ? Sev::Warn : Sev::Good, personal, s.id,
               "Underwriters " + verb + " " + s.name + " (" + zoneName(s.zone) + ") from " + G[m.from] + " to " + G[m.to] +
                   (m.to > m.from ? ": insurance there costs more from today." : "."));
        if (!personal) shown++;
    }
}

double World::shipRisk(const Ship& s) const {
    double risk;
    if (s.fleet != NONE) {
        const Fleet& f = fleets[s.fleet];
        risk = systems[f.system].risk;
        if (f.order.system != NONE) risk = std::max(risk, (double)systems[f.order.system].risk);
        if (f.order.station != NONE) risk = std::max(risk, (double)sysOf(f.order.station).risk);
        if (f.order.station2 != NONE) risk = std::max(risk, (double)sysOf(f.order.station2).risk);
    } else {
        risk = s.station != NONE ? sysOf(s.station).risk : 0.0;
    }
    return risk;
}

Money World::insurancePremium(const Ship& s) const { return (Money)(shipValue(s) * (0.03 + 0.002 * shipRisk(s))); }

}  // namespace nb
