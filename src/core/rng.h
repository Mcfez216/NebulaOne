// NebulaOne - small deterministic random generator (xoshiro256**).
// One instance lives in the World and is saved with it, so a save file
// always continues the same way.
#pragma once
#include <cstdint>
#include <cmath>

namespace nb {

struct Rng {
    uint64_t s[4] = {0x9E3779B97F4A7C15ull, 0xBF58476D1CE4E5B9ull, 0x94D049BB133111EBull, 0x2545F4914F6CDD1Dull};

    static uint64_t splitmix(uint64_t& x) {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    void seed(uint64_t v) {
        for (auto& w : s) w = splitmix(v);
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
    // [0, 1)
    double uniform() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    // [lo, hi] inclusive
    int irange(int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); }
    bool chance(double p) { return uniform() < p; }
    double normal(double mean, double sd) {
        double u1 = uniform(), u2 = uniform();
        if (u1 < 1e-12) u1 = 1e-12;
        return mean + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
    template <class A>
    void ser(A& a) {
        for (auto& w : s) a(w);
    }
};

}  // namespace nb
