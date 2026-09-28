#pragma once

// ============================================================
// Benchmark datasets, for every key type
// ============================================================
// One generator per input SHAPE, templated on the key type, deterministic
// for a given (shape, n, seed). Shapes that need an order (sorted,
// reversed, nearly sorted...) are built by sorting random keys of the
// type itself, so they are meaningful for 8-bit keys as well as 64-bit
// ones - a sorted uint8_t input is 256 runs of duplicates, not a
// wrapped-around ramp.
//
// The shapes, and why each is here:
//
//   random            uniform over the whole type: high entropy
//   sorted            already ascending
//   reversed          strictly/weakly descending
//   nearly_sorted     sorted, then 1% of positions swapped with random others
//   local_disorder    sorted, then every element swapped with a neighbour
//                     at distance <= 8: disorder everywhere, but short
//   few_outliers      sorted, then 0.01% of the elements replaced by
//                     random keys: "a few elements out of place"
//   sorted_tail       99% sorted, then 1% of random keys appended
//   organ_pipe        ascending then descending
//   sawtooth          16 ascending runs
//   duplicates        5 distinct keys
//   binary            2 distinct keys: one bit of entropy
//   low_entropy       AND of four random words: most bits zero
//   normal            Gaussian, sd = 1/16 of the type's range
//   clustered         99% in a narrow band, 1% far outliers
//   whole_universe    uniform, plus the type's minimum and maximum
//   adversarial       Stratum's depth-exhausting input (64-bit keys only):
//                     every refinement level peels one element
//   worst_case        the same construction with a large core, which
//                     produces the largest leaves the bound allows: the
//                     input that sends the most work to introsort
//   qsort_killer      McIlroy's adversary against this platform's std::sort,
//                     which drives it into its heapsort fallback: a worst
//                     case for the comparison baseline, not for Stratum
// ============================================================

#include "DatasetGenerator.hpp"
#include "stratum/Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace stratum {
namespace bench {

inline const std::vector<std::string>& allShapes() {
    static const std::vector<std::string> shapes = {
        "random",      "sorted",      "reversed",   "nearly_sorted", "local_disorder",
        "few_outliers", "sorted_tail", "organ_pipe", "sawtooth",      "duplicates",
        "binary",      "low_entropy", "normal",     "clustered",     "whole_universe",
        "adversarial", "worst_case",  "qsort_killer"};
    return shapes;
}

template <typename T>
T randomKey(std::mt19937_64& rng) {
    return static_cast<T>(rng());
}

// A random key of type T spread over the whole type, including for signed
// types (a plain cast of a 64-bit word already is).
template <typename T>
std::vector<T> randomKeys(std::size_t n, std::mt19937_64& rng) {
    std::vector<T> v(n);
    for (auto& x : v) x = randomKey<T>(rng);
    return v;
}

// McIlroy's adversary ("A Killer Adversary for Quicksort", 1999), run
// against THIS platform's std::sort at generation time: every comparison is
// decided as late as possible, so the resulting order is the one that is
// worst for whatever std::sort the benchmark is compared with - libstdc++,
// libc++ and MSVC each get their own. It drives an introsort into its
// heapsort fallback. Keys are ranks 0..n-1, rescaled into T when n exceeds
// T's range (which only adds duplicates).
template <typename T>
std::vector<T> quicksortKiller(std::size_t n) {
    std::vector<std::size_t> val(n, n), ptr(n);
    for (std::size_t i = 0; i < n; ++i) ptr[i] = i;
    const std::size_t gas = n;
    std::size_t solid = 0, candidate = 0;
    std::sort(ptr.begin(), ptr.end(), [&](std::size_t x, std::size_t y) {
        if (val[x] == gas && val[y] == gas) {
            if (x == candidate) val[x] = solid++;
            else val[y] = solid++;
        }
        if (val[x] == gas) candidate = x;
        else if (val[y] == gas) candidate = y;
        return val[x] < val[y];
    });
    for (std::size_t i = 0; i < n; ++i)
        if (val[i] == gas) val[i] = solid++;
    using U = typename std::make_unsigned<T>::type;
    const long double room = static_cast<long double>(std::numeric_limits<U>::max());
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        long double r = static_cast<long double>(val[i]);
        if (static_cast<long double>(n) > room) r = r * room / static_cast<long double>(n);
        v[i] = static_cast<T>(static_cast<U>(static_cast<uint64_t>(r)));
    }
    return v;
}

template <typename T>
std::vector<T> makeShape(const std::string& shape, std::size_t n, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    using U = typename std::make_unsigned<T>::type;
    const T lo = std::numeric_limits<T>::min();
    const T hi = std::numeric_limits<T>::max();

    if (shape == "random") return randomKeys<T>(n, rng);
    if (shape == "sorted" || shape == "reversed" || shape == "nearly_sorted" ||
        shape == "local_disorder" || shape == "few_outliers") {
        std::vector<T> v = randomKeys<T>(n, rng);
        std::sort(v.begin(), v.end());
        if (shape == "reversed") std::reverse(v.begin(), v.end());
        if (shape == "nearly_sorted" && n > 1) {
            const std::size_t swaps = std::max<std::size_t>(1, n / 100);
            for (std::size_t s = 0; s < swaps; ++s) std::swap(v[rng() % n], v[rng() % n]);
        }
        if (shape == "local_disorder" && n > 1) {
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = std::min(n - 1, i + static_cast<std::size_t>(rng() % 9));
                std::swap(v[i], v[j]);
            }
        }
        if (shape == "few_outliers" && n > 0) {
            const std::size_t k = std::max<std::size_t>(1, n / 10000);
            for (std::size_t s = 0; s < k; ++s) v[rng() % n] = randomKey<T>(rng);
        }
        return v;
    }
    if (shape == "sorted_tail") {
        std::vector<T> v = randomKeys<T>(n, rng);
        const std::size_t head = n - n / 100;
        std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(head));
        return v;
    }
    if (shape == "organ_pipe") {
        std::vector<T> v = randomKeys<T>(n, rng);
        std::sort(v.begin(), v.end());
        std::vector<T> out(n);
        std::size_t l = 0, r = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (i % 2 == 0) out[l++] = v[i];
            else out[--r] = v[i];
        }
        return out;
    }
    if (shape == "sawtooth") {
        std::vector<T> v = randomKeys<T>(n, rng);
        const std::size_t runs = 16;
        for (std::size_t r = 0; r < runs; ++r) {
            const std::size_t b = n * r / runs, e = n * (r + 1) / runs;
            std::sort(v.begin() + static_cast<std::ptrdiff_t>(b), v.begin() + static_cast<std::ptrdiff_t>(e));
        }
        return v;
    }
    if (shape == "duplicates") {
        std::vector<T> v(n);
        for (auto& x : v) x = static_cast<T>(static_cast<U>(rng() % 5));
        return v;
    }
    if (shape == "binary") {
        std::vector<T> v(n);
        for (auto& x : v) x = static_cast<T>(static_cast<U>(rng() & 1));
        return v;
    }
    if (shape == "low_entropy") {
        std::vector<T> v(n);
        for (auto& x : v) x = static_cast<T>(rng() & rng() & rng() & rng());
        return v;
    }
    if (shape == "normal") {
        // Centred in the type, sd = range / 16, clamped into the type.
        const long double range = static_cast<long double>(hi) - static_cast<long double>(lo);
        const long double mid = static_cast<long double>(lo) + range / 2;
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<T> v(n);
        for (auto& x : v) {
            long double y = mid + static_cast<long double>(dist(rng)) * range / 16;
            // Clamp strictly inside the type: converting the result back
            // must not overflow, and the extremes of a 64-bit type are not
            // exactly representable where long double is a double.
            const long double lower = static_cast<long double>(lo) + range / 1024;
            const long double upper = static_cast<long double>(hi) - range / 1024;
            y = std::min(std::max(y, lower), upper);
            x = static_cast<T>(y);
        }
        return v;
    }
    if (shape == "clustered") {
        std::vector<T> v(n);
        const uint64_t base = rng();
        for (auto& x : v)
            x = (rng() % 100 == 0) ? randomKey<T>(rng) : static_cast<T>(base + rng() % 101);
        return v;
    }
    if (shape == "whole_universe") {
        std::vector<T> v = randomKeys<T>(n, rng);
        if (n > 0) v[0] = lo;
        if (n > 1) v[n - 1] = hi;
        std::shuffle(v.begin(), v.end(), rng);
        return v;
    }
    if (shape == "adversarial" || shape == "worst_case") {
        // Built for the DEFAULT lambda: an adversary built for a different
        // lambda is just another random input.
        testing::DatasetGenerator g(seed);
        const auto a = shape == "adversarial"
                           ? g.adversarialPeeling(n, DEFAULT_TARGET_ELEMENTS_PER_BIN)
                           : g.adversarialPeeling(n, DEFAULT_TARGET_ELEMENTS_PER_BIN, 4096);
        std::vector<T> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(a[i]);
        return v;
    }
    if (shape == "qsort_killer") return quicksortKiller<T>(n);
    return {};
}

// The adversarial constructions spend the 64-bit span budget; on a
// narrower key they degenerate into ordinary inputs and are skipped.
template <typename T>
bool shapeApplies(const std::string& shape) {
    if (shape == "adversarial" || shape == "worst_case") return sizeof(T) == 8;
    return true;
}

} // namespace bench
} // namespace stratum
