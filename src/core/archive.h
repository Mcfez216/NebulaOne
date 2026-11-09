// NebulaOne - tiny binary archive used for save games.
// Each struct lists its fields once in `template<class A> void ser(A& a)`;
// the same list drives both writing and reading.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nb {

template <class T> struct is_vec : std::false_type {};
template <class T, class Al> struct is_vec<std::vector<T, Al>> : std::true_type {};
template <class T> struct is_deq : std::false_type {};
template <class T, class Al> struct is_deq<std::deque<T, Al>> : std::true_type {};
template <class T> struct is_map : std::false_type {};
template <class K, class V, class C, class Al> struct is_map<std::map<K, V, C, Al>> : std::true_type {};
template <class T> struct is_arr : std::false_type {};
template <class T, size_t N> struct is_arr<std::array<T, N>> : std::true_type {};
template <class T> struct is_pair : std::false_type {};
template <class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};

template <class Ar, class T>
void io(Ar& ar, T& v) {
    if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        ar.raw(&v, sizeof(T));
    } else if constexpr (std::is_same_v<T, std::string>) {
        uint32_t n = (uint32_t)v.size();
        ar.raw(&n, 4);
        if constexpr (Ar::reading) { ar.check(n); v.resize(n); }
        if (n) ar.raw(v.data(), n);
    } else if constexpr (is_vec<T>::value || is_deq<T>::value) {
        uint32_t n = (uint32_t)v.size();
        ar.raw(&n, 4);
        if constexpr (Ar::reading) { ar.check(n); v.clear(); v.resize(n); }
        for (auto& e : v) io(ar, e);
    } else if constexpr (is_map<T>::value) {
        uint32_t n = (uint32_t)v.size();
        ar.raw(&n, 4);
        if constexpr (Ar::reading) {
            ar.check(n);
            v.clear();
            for (uint32_t i = 0; i < n; i++) {
                typename T::key_type k{};
                typename T::mapped_type m{};
                io(ar, k);
                io(ar, m);
                v.emplace(std::move(k), std::move(m));
            }
        } else {
            for (auto& kv : v) {
                auto k = kv.first;
                io(ar, k);
                io(ar, kv.second);
            }
        }
    } else if constexpr (is_arr<T>::value) {
        for (auto& e : v) io(ar, e);
    } else if constexpr (is_pair<T>::value) {
        io(ar, v.first);
        io(ar, v.second);
    } else {
        v.ser(ar);
    }
}

struct Writer {
    static constexpr bool reading = false;
    std::vector<uint8_t> buf;
    void raw(const void* p, size_t n) {
        auto b = (const uint8_t*)p;
        buf.insert(buf.end(), b, b + n);
    }
    void check(uint32_t) {}
    template <class... T> void operator()(T&... v) { (io(*this, v), ...); }
};

struct Reader {
    static constexpr bool reading = true;
    const uint8_t* p;
    size_t left;
    Reader(const std::vector<uint8_t>& b, size_t offset = 0) : p(b.data() + offset), left(b.size() - offset) {}
    void raw(void* dst, size_t n) {
        if (n > left) throw std::runtime_error("save file is truncated or corrupt");
        std::memcpy(dst, p, n);
        p += n;
        left -= n;
    }
    // guard against absurd element counts from a corrupt file
    void check(uint32_t n) {
        if (n > 50000000u) throw std::runtime_error("save file is corrupt (bad count)");
    }
    template <class... T> void operator()(T&... v) { (io(*this, v), ...); }
};

}  // namespace nb
