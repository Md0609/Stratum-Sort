#pragma once

// ============================================================
// The objective.md section 4 battery, in one place
// ============================================================
// Every tool that claims to measure "all the benchmarks" - benchmarks/
// timings.cpp, the alternating A/B (research/tools/ab_compare.sh, which
// drives timings), the differential fuzz and the research counters - takes
// its datasets from here, so the battery cannot drift between them.
//
//   current (10)   the ten historical datasets, generated BIT-FOR-BIT as
//                  before for int64_t (they come straight from
//                  DatasetGenerator, which this file does not modify);
//   new (8)        NearlySorted, SortedPlusTail, OrganPipe, Sawtooth,
//                  FewDistinct x3 (2/16/256), AllEqual;
//   adversary (8)  the eight families of research/tools/AdversaryBattery.
//
// THE ADVERSARIES ARE BUILT AGAINST THE CONFIGURATION BEING MEASURED.
// AdversarialPeeling and adversary families 7 and 8 depend on lambda and
// on the depth cap D; an adversary built for another lambda is just another
// random input (BENCHMARKS.md, "Known measurement traps"). This header
// deliberately does NOT include the library: the caller passes the lambda
// and D of the build it is timing, so a harness compiled against another
// commit's headers rebuilds the adversary for that commit.
//
// Key types. Everything is a template on the key type T. For int64_t the
// ten current datasets are DatasetGenerator's own output. For other types
// the datasets whose values fit (all but three) are DatasetGenerator's
// values converted to T; HugeRangeFewEl, FullRangeExtremes and
// AdversarialPeeling are re-derived from T's own key universe, because
// their whole point is the span of the type. For key types narrower than
// the values (the 8- and 16-bit types the fuzz uses) the conversion wraps:
// the shape is lost, the input is still a valid test case.
//
// Some adversary families cannot be built at every size: family 7 needs a
// core larger than n at small n, family 8 needs at least two groups. Those
// return an EMPTY vector, and every caller reports them as "n/a" rather
// than inventing a substitute. Family 8 also returns fewer than n elements
// by construction (k whole groups); callers report the actual size.
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace stratum::testing {

// The configuration the adversaries are built against: the lambda the
// sorter will actually use (after the constructor's clamping) and the
// depth cap MAX_SUBDIVISION_DEPTH of the headers being measured.
struct AdversaryTarget {
    std::size_t lambda;
    std::size_t depth;
};

namespace battery_detail {

inline uint64_t ipow(uint64_t s, uint64_t k) {
    uint64_t r = 1;
    for (uint64_t i = 0; i < k; ++i) r *= s;
    return r;
}
inline bool fits(uint64_t s, uint64_t k, uint64_t lim) {
    uint64_t r = 1;
    for (uint64_t i = 0; i < k; ++i) {
        if (r > lim / s) return false;
        r *= s;
    }
    return true;
}
// Largest value of the unsigned counterpart of T, i.e. 2^w - 1.
template <typename T>
uint64_t universeMax() {
    return static_cast<uint64_t>(std::numeric_limits<std::make_unsigned_t<T>>::max());
}
// The key at offset 'off' from the minimum of T, wrapping modulo 2^w.
template <typename T>
T fromMin(uint64_t off) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(static_cast<U>(std::numeric_limits<T>::min()) + off));
}
// The fan-out search below is monotone; the cap only matters for D = 0,
// which H1 excludes, so that a bad argument cannot hang a benchmark.
constexpr uint64_t kMaxFanOut = uint64_t{1} << 24;

template <typename T, typename F>
std::vector<T> generate(std::size_t n, F f) {
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(f(i));
    return v;
}

template <typename T>
std::vector<T> convert(const DataVector& in) {
    std::vector<T> v(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) v[i] = static_cast<T>(in[i]);
    return v;
}

// Uniform draw over [lo, hi] for any integral T, via a 64-bit distribution
// so that 8-bit types are supported (uniform_int_distribution is not
// defined for char types).
template <typename T>
T uniformKey(std::mt19937_64& rng, T lo, T hi) {
    if constexpr (std::is_signed_v<T>) {
        return static_cast<T>(std::uniform_int_distribution<long long>(lo, hi)(rng));
    } else {
        return static_cast<T>(std::uniform_int_distribution<unsigned long long>(lo, hi)(rng));
    }
}

} // namespace battery_detail

// ------------------------------------------------------------
// The eight AdversaryBattery families (research/tools/AdversaryBattery.cpp
// uses exactly these, with its own rng, so its output is unchanged).
// 'family' is 1..8; the comments are that tool's descriptions.
// ------------------------------------------------------------
template <typename T>
std::vector<T> adversaryFamily(int family, std::size_t n, const AdversaryTarget& cfg,
                               std::mt19937_64& rng) {
    using namespace battery_detail;
    const uint64_t L = cfg.lambda < 1 ? 1 : cfg.lambda;
    const uint64_t D = cfg.depth;
    const uint64_t umax = universeMax<T>();
    std::vector<T> v;
    switch (family) {
        case 1: // all equal
            v.assign(n, static_cast<T>(42));
            break;
        case 2: // all distinct
            v = generate<T>(n, [](std::size_t i) { return i; });
            std::shuffle(v.begin(), v.end(), rng);
            break;
        case 3: // massive duplicates
            v.resize(n);
            for (auto& x : v) x = static_cast<T>(rng() % 8);
            break;
        case 4: // one dominant value plus 0.1% random outliers
            v.assign(n, static_cast<T>(1000));
            for (std::size_t i = 0; i < n / 1000; ++i) v[i * 1000] = static_cast<T>(rng());
            std::shuffle(v.begin(), v.end(), rng);
            break;
        case 5: // alternating 0/1
            v = generate<T>(n, [](std::size_t i) { return i % 2; });
            break;
        case 6: { // everything in one bucket except one far value
            const uint64_t far = std::min<uint64_t>(1000000000000000000ULL,
                                                    static_cast<uint64_t>(std::numeric_limits<T>::max()));
            v.resize(n);
            for (std::size_t i = 0; i < n; ++i)
                v[i] = (i + 1 < n) ? static_cast<T>(rng() % 4) : static_cast<T>(far);
            break;
        }
        case 7: { // maximal leaf: saturates the residual bound B(n)
            if (n == 0) break;
            const uint64_t sTop = (n + L - 1) / L;
            const uint64_t lim = umax / sTop;
            uint64_t s = 2;
            while (s < kMaxFanOut && fits(s + 1, D, lim)) ++s;
            const uint64_t C = s * L > D ? s * L - D : 0;
            if (C == 0 || C + D > n) break; // not constructible at this n: n/a
            v.reserve(n);
            for (uint64_t i = 0; i < C; ++i) v.push_back(fromMin<T>(i % 2 == 0 ? 1 : 0));
            for (uint64_t k = 1; k <= D; ++k) v.push_back(fromMin<T>(ipow(s, k)));
            while (v.size() < n) v.push_back(fromMin<T>(sTop * ipow(s, D)));
            break;
        }
        case 8: { // total cost: family 7 replicated k times
            if (n == 0) break;
            uint64_t s = 2;
            while (s < kMaxFanOut && fits(s + 1, D, (umax / 2 / n) * L)) ++s;
            const uint64_t k = n / (s * L);
            if (k < 2 || s * L <= D) break; // not constructible at this n: n/a
            const uint64_t C = s * L - D, stride = 2 * ipow(s, D + 1);
            v.reserve(k * s * L);
            for (uint64_t g = 0; g < k; ++g) {
                const uint64_t b = g * stride;
                for (uint64_t i = 0; i < C; ++i) v.push_back(fromMin<T>(b + (i % 2 == 0 ? 1 : 0)));
                for (uint64_t q = 1; q <= D; ++q) v.push_back(fromMin<T>(b + ipow(s, q)));
            }
            break;
        }
        default:
            break;
    }
    return v;
}

// ------------------------------------------------------------
// Type-generic versions of the three datasets defined by the key
// universe. Only used for T != int64_t; for int64_t the battery calls
// DatasetGenerator itself so the historical inputs do not change.
// ------------------------------------------------------------
template <typename T>
std::vector<T> hugeRangeFewElementsOf(std::size_t n, std::mt19937_64& rng) {
    const T lo = static_cast<T>(std::numeric_limits<T>::min() / 2);
    const T hi = static_cast<T>(std::numeric_limits<T>::max() / 2);
    std::vector<T> v(n);
    for (auto& x : v) x = battery_detail::uniformKey<T>(rng, lo, hi);
    return v;
}

template <typename T>
std::vector<T> fullRangeExtremesOf(std::size_t n, std::mt19937_64& rng) {
    const T lo = std::numeric_limits<T>::min();
    const T hi = std::numeric_limits<T>::max();
    std::vector<T> v(n);
    if (n == 0) return v;
    for (auto& x : v) x = battery_detail::uniformKey<T>(rng, lo, hi);
    v[0] = lo;
    if (n > 1) v[n - 1] = hi;
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// DatasetGenerator::adversarialPeeling with the key universe of T in place
// of int64_t's (only the non-negative half for a signed T, as there).
template <typename T>
std::vector<T> adversarialPeelingOf(std::size_t n, std::size_t target, std::mt19937_64& rng) {
    std::vector<T> v;
    v.reserve(n);
    if (n == 0) return v;
    if (target < 1) target = 1;
    const std::size_t coreSize = std::min(target, n);
    std::size_t groupsEstimate = n / std::max<std::size_t>(coreSize * 2, 1);
    if (groupsEstimate == 0) groupsEstimate = 1;
    const uint64_t budget =
        (static_cast<uint64_t>(std::numeric_limits<T>::max()) / groupsEstimate) / 2;

    std::vector<uint64_t> offsets;
    uint64_t span = 0;
    for (std::size_t i = 0; i < coreSize; ++i) {
        offsets.push_back(static_cast<uint64_t>(i));
        span = static_cast<uint64_t>(i);
    }
    while (offsets.size() < n) {
        const std::size_t sizeAfter = offsets.size() + 1;
        const uint64_t splits = (sizeAfter + target - 1) / target;
        if (splits < 2) break;
        if (span > budget / splits) break;
        const uint64_t next = span * splits;
        if (next <= span) break;
        offsets.push_back(next);
        span = next;
    }
    const uint64_t stride = span + 2;
    uint64_t base = 0;
    while (v.size() < n) {
        for (uint64_t off : offsets) {
            if (v.size() == n) break;
            v.push_back(static_cast<T>(base + off));
        }
        base += stride;
    }
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// ------------------------------------------------------------
// The battery
// ------------------------------------------------------------
struct BatteryEntry {
    const char* name;
    const char* group; // "current", "new" or "adversary"
};

inline const std::vector<BatteryEntry>& battery() {
    static const std::vector<BatteryEntry> entries = {
        {"RandomUniform", "current"},       {"SortedAscending", "current"},
        {"SortedDescending", "current"},    {"ManyRepeated", "current"},
        {"NormalGaussian", "current"},      {"Concentrated", "current"},
        {"SmallRangeManyEl", "current"},    {"HugeRangeFewEl", "current"},
        {"FullRangeExtremes", "current"},   {"AdversarialPeeling", "current"},
        {"NearlySorted", "new"},            {"SortedPlusTail", "new"},
        {"OrganPipe", "new"},               {"Sawtooth", "new"},
        {"FewDistinct2", "new"},            {"FewDistinct16", "new"},
        {"FewDistinct256", "new"},          {"AllEqual", "new"},
        {"Adv1AllEqual", "adversary"},      {"Adv2AllDistinct", "adversary"},
        {"Adv3MassiveDup", "adversary"},    {"Adv4DominantOutliers", "adversary"},
        {"Adv5Alternating", "adversary"},   {"Adv6OneBucketFar", "adversary"},
        {"Adv7MaximalLeaf", "adversary"},   {"Adv8TotalCost", "adversary"},
    };
    return entries;
}

// Index of a dataset by name, or battery().size() if there is none.
inline std::size_t batteryIndex(const std::string& name) {
    const auto& b = battery();
    for (std::size_t i = 0; i < b.size(); ++i)
        if (name == b[i].name) return i;
    return b.size();
}

// Builds dataset 'index' of battery() with n elements (fewer, or none, for
// adversary families 7 and 8 - see the top of this file). Each dataset is
// generated from its own fresh generator seeded with 'seed', so a dataset
// does not depend on which others were generated before it.
template <typename T>
std::vector<T> batteryDataset(std::size_t index, std::size_t n, const AdversaryTarget& cfg,
                              uint64_t seed = 42) {
    using namespace battery_detail;
    constexpr bool kInt64 = std::is_same_v<T, int64_t>;
    DatasetGenerator g(seed);
    std::mt19937_64 rng(seed);
    switch (index) {
        case 0: return convert<T>(g.randomUniform(n));
        case 1: return convert<T>(g.sortedAscending(n));
        case 2: return convert<T>(g.sortedDescending(n));
        case 3: return convert<T>(g.manyRepeated(n));
        case 4: return convert<T>(g.normalDistribution(n));
        case 5: return convert<T>(g.concentrated(n));
        case 6: return convert<T>(g.smallRangeManyElements(n));
        case 7:
            if constexpr (kInt64) return g.hugeRangeFewElements(n);
            else return hugeRangeFewElementsOf<T>(n, rng);
        case 8:
            if constexpr (kInt64) return g.fullRangeExtremes(n);
            else return fullRangeExtremesOf<T>(n, rng);
        case 9:
            if constexpr (kInt64) return g.adversarialPeeling(n, cfg.lambda);
            else return adversarialPeelingOf<T>(n, cfg.lambda, rng);

        case 10: { // NearlySorted: ascending, then n/100 random transpositions (1%)
            std::vector<T> v = generate<T>(n, [](std::size_t i) { return i; });
            if (n >= 2) {
                std::uniform_int_distribution<std::size_t> pos(0, n - 1);
                for (std::size_t k = 0; k < n / 100; ++k) {
                    const std::size_t a = pos(rng), b = pos(rng);
                    std::swap(v[a], v[b]);
                }
            }
            return v;
        }
        case 11: { // SortedPlusTail: ascending, last 1% replaced by random values in [0, n)
            std::vector<T> v = generate<T>(n, [](std::size_t i) { return i; });
            if (n >= 1) {
                std::uniform_int_distribution<std::size_t> val(0, n - 1);
                for (std::size_t i = n - n / 100; i < n; ++i) v[i] = static_cast<T>(val(rng));
            }
            return v;
        }
        case 12: // OrganPipe: 0,1,...,peak,...,1,0
            return generate<T>(n, [n](std::size_t i) { return i < (n + 1) / 2 ? i : n - 1 - i; });
        case 13: // Sawtooth: ascending runs of 1000
            return generate<T>(n, [](std::size_t i) { return i % 1000; });
        case 14:
        case 15:
        case 16: { // FewDistinct k: k values evenly spread over [0, 2^30), uniform choice
            const uint64_t k = index == 14 ? 2 : index == 15 ? 16 : 256;
            const uint64_t step = (uint64_t{1} << 30) / k;
            std::uniform_int_distribution<uint64_t> pick(0, k - 1);
            std::vector<T> v(n);
            for (auto& x : v) x = static_cast<T>(pick(rng) * step);
            return v;
        }
        case 17: // AllEqual
            return std::vector<T>(n, static_cast<T>(42));
        default:
            if (index >= 18 && index < 26)
                return adversaryFamily<T>(static_cast<int>(index - 17), n, cfg, rng);
            return {};
    }
}

} // namespace stratum::testing
