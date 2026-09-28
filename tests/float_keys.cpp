#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// ============================================================
// Floating-point keys: the proof, then the sorts
// ============================================================
// OrderedKey<float> maps a float to a 32-bit key by flipping bits. The
// claim is that ordering floats by that key IS the IEEE 754-2008 totalOrder
// predicate, and that the map is a bijection on bit patterns. This file
// does not argue it, it checks it:
//
//   1. EXHAUSTIVELY for float. Every one of the 2^32 bit patterns
//      round-trips (decode(encode(x)) == x, bit for bit), and walking all
//      2^32 keys in increasing order yields floats in strictly increasing
//      totalOrder - judged by referenceTotalOrder() below, which is written
//      from the standard's definition (sign, NaN-ness, numeric comparison,
//      the -0/+0 rule, payload order) and never looks at the key map.
//      A strictly increasing sequence over all 2^32 values, together with
//      the bijection, is the whole claim: by transitivity every pair of
//      floats is ordered by key exactly as totalOrder orders it.
//   2. For double, where 2^64 is out of reach: every pair from a set of
//      special values and their neighbours, long nextafter chains through
//      every region (denormals, around 0, around 1, near the maximum,
//      through the NaNs), and millions of random pairs.
//   3. Sorting: stratum::sort and stable_sort on float and double vectors
//      full of the hard values, compared BIT FOR BIT with std::sort under
//      referenceTotalOrder, and std::is_sorted under operator< on NaN-free
//      inputs (so the output is also a valid std::sort output).
//
// Optional argument: a stride for part 1 (default 1 = exhaustive). CTest
// runs it exhaustively; the loop is plain bit arithmetic.
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {

long long g_checks = 0;
long long g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok && ++g_failures <= 20) std::printf("  FAIL: %s\n", what);
}

template <typename F, typename Bits>
Bits bitsOf(F x) {
    Bits b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}

// IEEE 754-2008, 5.10 totalOrder(x, y), returned as -1 / 0 / +1:
//   a) numeric order for non-NaN operands, with -0 < +0;
//   b) negative NaNs below everything, positive NaNs above everything;
//   c) between two NaNs of the same sign, the one with the greater payload
//      (including the quiet bit, which makes quiet above signaling) is
//      above for +NaN and below for -NaN.
// Deliberately written from those clauses, with std::isnan, std::signbit
// and operator<, not from the bit-flip the library uses.
template <typename F, typename Bits>
int referenceTotalOrder(F x, F y) {
    const bool nx = std::isnan(x), ny = std::isnan(y);
    const bool sx = std::signbit(x), sy = std::signbit(y);
    if (!nx && !ny) {
        if (x < y) return -1;
        if (y < x) return 1;
        if (sx != sy) return sx ? -1 : 1; // -0 before +0
        return 0;
    }
    if (nx && !ny) return sx ? -1 : 1;
    if (!nx && ny) return sy ? 1 : -1;
    if (sx != sy) return sx ? -1 : 1;
    const Bits mantissaMask = (Bits{1} << (std::numeric_limits<F>::digits - 1)) - 1;
    const Bits px = bitsOf<F, Bits>(x) & mantissaMask;
    const Bits py = bitsOf<F, Bits>(y) & mantissaMask;
    if (px == py) return 0;
    const int positiveOrder = px < py ? -1 : 1;
    return sx ? -positiveOrder : positiveOrder;
}

// ---- 1. float, exhaustively ------------------------------------------
void exhaustiveFloat(uint64_t stride) {
    using K = stratum::detail::FloatKey<float, uint32_t>;
    std::printf("1. float: all 2^32 patterns%s\n", stride == 1 ? "" : " (strided)");
    long long roundTripFailures = 0, orderFailures = 0, nanCount = 0;
    float previous = K::decode(0);
    for (uint64_t k64 = 0; k64 <= 0xFFFFFFFFull; k64 += stride) {
        const uint32_t k = static_cast<uint32_t>(k64);
        const float f = K::decode(k);
        // Bijection: the key of the decoded float is k again, and the
        // decoded float's bits round-trip through encode.
        if (K::encode(f) != k) ++roundTripFailures;
        if (std::isnan(f)) ++nanCount;
        if (k64 != 0 && referenceTotalOrder<float, uint32_t>(previous, f) != -1) {
            if (++orderFailures <= 5)
                std::printf("  order broken at key %08x: %a !< %a\n", k, static_cast<double>(previous),
                            static_cast<double>(f));
        }
        previous = f;
    }
    check(roundTripFailures == 0, "every float bit pattern round-trips through its key");
    check(orderFailures == 0, "walking all keys in order gives strictly increasing totalOrder");
    if (stride == 1) {
        // 2^24 - 2 NaN patterns: the exponent all ones, any non-zero mantissa,
        // either sign. Checks the walk really visited every value.
        check(nanCount == (1ll << 24) - 2, "the walk met every NaN pattern exactly once");
    }
    // Where the landmarks sit in key order.
    check(K::encode(-std::numeric_limits<float>::infinity()) + 1 ==
              K::encode(-std::numeric_limits<float>::max()),
          "-inf is immediately below -FLT_MAX");
    check(K::encode(-0.0f) + 1 == K::encode(0.0f), "-0 is immediately below +0");
    check(K::encode(std::numeric_limits<float>::max()) + 1 ==
              K::encode(std::numeric_limits<float>::infinity()),
          "+inf is immediately above FLT_MAX");
}

// ---- 2. double -----------------------------------------------------------
std::vector<double> doubleSpecials() {
    const double inf = std::numeric_limits<double>::infinity();
    const double dmin = std::numeric_limits<double>::denorm_min();
    const double nmin = std::numeric_limits<double>::min();
    const double dmax = std::numeric_limits<double>::max();
    std::vector<double> v = {0.0, -0.0, dmin, -dmin, nmin, -nmin, dmax, -dmax, inf, -inf,
                             1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 1e308, -1e308, 1e-308, -1e-308,
                             std::numeric_limits<double>::quiet_NaN(),
                             -std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::signaling_NaN(),
                             -std::numeric_limits<double>::signaling_NaN()};
    // NaNs with chosen payloads, both signs.
    for (uint64_t payload : {uint64_t{1}, uint64_t{2}, uint64_t{0x7FFFF}, (uint64_t{1} << 51) - 1,
                             uint64_t{1} << 51, (uint64_t{1} << 52) - 1}) {
        for (uint64_t sign : {uint64_t{0}, uint64_t{1} << 63}) {
            const uint64_t bits = sign | (uint64_t{0x7FF} << 52) | payload;
            double d;
            std::memcpy(&d, &bits, sizeof d);
            v.push_back(d);
        }
    }
    const std::size_t base = v.size();
    for (std::size_t i = 0; i < base; ++i) {
        if (std::isnan(v[i])) continue;
        v.push_back(std::nextafter(v[i], inf));
        v.push_back(std::nextafter(v[i], -inf));
    }
    return v;
}

void doubleKeys() {
    using K = stratum::detail::FloatKey<double, uint64_t>;
    std::printf("2. double: specials, nextafter chains, random pairs\n");
    const std::vector<double> sp = doubleSpecials();
    long long bad = 0;
    for (double a : sp) {
        for (double b : sp) {
            const int want = referenceTotalOrder<double, uint64_t>(a, b);
            const uint64_t ka = K::encode(a), kb = K::encode(b);
            const int got = ka < kb ? -1 : ka > kb ? 1 : 0;
            if (want != got) ++bad;
        }
        if (bitsOf<double, uint64_t>(K::decode(K::encode(a))) != bitsOf<double, uint64_t>(a)) ++bad;
    }
    check(bad == 0, "every pair of special doubles is ordered by key as by totalOrder");

    // nextafter chains: consecutive doubles have consecutive keys - except
    // across zero, where nextafter steps from -0 (or from -denorm_min) over
    // the other zero, since -0 == +0 numerically: that step is +2 in key
    // space, the skipped key being the other zero.
    bad = 0;
    const double inf = std::numeric_limits<double>::infinity();
    for (double start : {-inf, -std::numeric_limits<double>::max(), -1.0, -std::numeric_limits<double>::min(),
                         -0.0, 0.0, std::numeric_limits<double>::denorm_min(), 1.0,
                         std::numeric_limits<double>::max() / 2}) {
        double x = start;
        for (int i = 0; i < 200000 && x < inf; ++i) {
            const double y = std::nextafter(x, inf);
            const uint64_t step = K::encode(y) - K::encode(x);
            const bool crossesZero = x == 0.0 || y == 0.0;
            if (crossesZero ? (step < 1 || step > 2) : step != 1) ++bad;
            x = y;
        }
    }
    check(bad == 0, "nextafter steps are +1 in key space through every region");

    std::mt19937_64 rng(0xD0B1E);
    bad = 0;
    for (int i = 0; i < 5000000; ++i) {
        uint64_t ba = rng(), bb = (i & 1) ? rng() : ba ^ (uint64_t{1} << (rng() % 64));
        double a, b;
        std::memcpy(&a, &ba, sizeof a);
        std::memcpy(&b, &bb, sizeof b);
        const int want = referenceTotalOrder<double, uint64_t>(a, b);
        const uint64_t ka = K::encode(a), kb = K::encode(b);
        if (want != (ka < kb ? -1 : ka > kb ? 1 : 0)) ++bad;
        if (bitsOf<double, uint64_t>(K::decode(ka)) != ba) ++bad;
    }
    check(bad == 0, "5e6 random double pairs: key order == totalOrder, and bits round-trip");
}

// ---- 3. sorting ---------------------------------------------------------
template <typename F, typename Bits>
std::vector<F> hardInput(std::size_t n, std::mt19937_64& rng, int shape) {
    std::vector<F> v(n);
    const F inf = std::numeric_limits<F>::infinity();
    const F nan = std::numeric_limits<F>::quiet_NaN();
    for (auto& x : v) {
        switch (shape) {
            case 0: { Bits b = static_cast<Bits>(rng()); std::memcpy(&x, &b, sizeof x); break; } // any pattern
            case 1: x = static_cast<F>(std::normal_distribution<double>(0.0, 1e3)(rng)); break;
            case 2: { const F pool[] = {F(0), -F(0), inf, -inf, nan, -nan, F(1), F(-1),
                                         std::numeric_limits<F>::denorm_min(), -std::numeric_limits<F>::denorm_min()};
                      x = pool[rng() % 10]; break; }
            default: x = static_cast<F>(static_cast<int>(rng() % 7) - 3) * F(0.5); break; // few distinct, with -0
        }
    }
    if (shape == 3) for (auto& x : v) if (x == F(0) && (rng() & 1)) x = -F(0);
    return v;
}

template <typename F, typename Bits>
void sortFloats(const char* name) {
    std::printf("3. sorting %s vectors\n", name);
    std::mt19937_64 rng(sizeof(F) * 1000 + 7);
    bool sameBits = true, validForLess = true;
    for (std::size_t n : {0u, 1u, 2u, 3u, 17u, 64u, 65u, 500u, 5000u, 100000u}) {
        for (int shape = 0; shape < 4; ++shape) {
            for (int stable = 0; stable < 2; ++stable) {
                std::vector<F> v = hardInput<F, Bits>(n, rng, shape);
                std::vector<F> want = v;
                std::sort(want.begin(), want.end(),
                          [](F a, F b) { return referenceTotalOrder<F, Bits>(a, b) < 0; });
                if (stable) stratum::stable_sort(v);
                else stratum::sort(v);
                if (v.size() != want.size() ||
                    (n > 0 && std::memcmp(v.data(), want.data(), n * sizeof(F)) != 0))
                    sameBits = false;
                bool hasNan = false;
                for (F x : v) hasNan = hasNan || std::isnan(x);
                if (!hasNan && !std::is_sorted(v.begin(), v.end())) validForLess = false;
            }
        }
    }
    check(sameBits, "sorted output is bit-for-bit std::sort under totalOrder");
    check(validForLess, "on NaN-free inputs the output is sorted under operator<");
}

} // namespace

int main(int argc, char** argv) {
    const uint64_t stride = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1;
    exhaustiveFloat(stride == 0 ? 1 : stride);
    doubleKeys();
    sortFloats<float, uint32_t>("float");
    sortFloats<double, uint64_t>("double");
    std::printf("%lld checks, %lld failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("Float keys: totalOrder proved exhaustively for float, checked for double.\n");
    return 0;
}
