// NebulaOne - basic types shared by the simulation core.
#pragma once
#include <cstdint>
#include <string>

namespace nb {

using Id = int32_t;
constexpr Id NONE = -1;

// Money is integer cents so ledgers never drift: 1 credit (cr) = 100.
using Money = int64_t;
constexpr Money CR = 100;
inline Money credits(double cr) { return (Money)(cr * 100.0 + (cr >= 0 ? 0.5 : -0.5)); }
inline double toCr(Money m) { return (double)m / 100.0; }

// One tick is one game minute.
using Tick = int64_t;
constexpr Tick MINUTES_PER_HOUR = 60;
constexpr Tick MINUTES_PER_DAY = 1440;

// Who keeps the law in a sector.
//   Charter  - the Charter Authority's Wardens respond to violence.
//   Contract - no public police; protection is whatever corporations pay for.
//   Frontier - unchartered space: no stations but the outposts people build.
enum class Zone : uint8_t { Charter = 0, Contract, Frontier };
constexpr int ZONE_COUNT = 3;

inline const char* zoneName(Zone z) {
    switch (z) {
        case Zone::Charter: return "Charter";
        case Zone::Contract: return "Contract";
        case Zone::Frontier: return "Frontier";
    }
    return "?";
}
inline const char* zoneLong(Zone z) {
    switch (z) {
        case Zone::Charter: return "Charter space";
        case Zone::Contract: return "Contract space";
        case Zone::Frontier: return "the Frontier";
    }
    return "?";
}

// Underwriters' risk rating: 0 (safest) .. 100, shown as a credit-style grade.
constexpr int RISK_GRADES = 8;
inline int riskGradeIndex(float risk) {
    static const float cut[RISK_GRADES - 1] = {10, 20, 30, 42, 55, 68, 82};
    int i = 0;
    while (i < RISK_GRADES - 1 && risk >= cut[i]) i++;
    return i;
}
inline const char* riskGrade(float risk) {
    static const char* g[RISK_GRADES] = {"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "D"};
    return g[riskGradeIndex(risk)];
}

// Format helpers used by both the UI and the self-test.
std::string fmtMoney(Money m, bool withUnit = true);       // "1,234,567.89 cr"
std::string fmtMoneyShort(Money m, bool withUnit = true);  // "1.23M cr"
std::string fmtInt(int64_t v);                             // "1,234,567"
std::string fmtQty(double v);                              // "12.4k"
std::string fmtDate(Tick t);                               // "3127.04.18 14:32"
std::string fmtDuration(Tick minutes);                     // "2h 15m"

}  // namespace nb
