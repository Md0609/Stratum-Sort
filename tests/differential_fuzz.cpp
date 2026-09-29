// A test binary compiled with NDEBUG would silently skip every internal
// invariant check: the bucket-index bound in countAndPlace and the leaf
// tiling verification in sort() are assert()s. That is exactly what a
// CMake Release build does, because CMake appends -DNDEBUG after any
// target flag. Forcing them on here makes the suites correct under every
// build system and configuration, at the cost of some speed - which a
// test should always trade away.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// ============================================================
// Differential fuzz against std::sort
// ============================================================
// The other two suites check what the author thought to check. This one
// checks what nobody thought of: it draws the element type, the size, both
// tuning parameters and the value distribution at random, sorts the same
// data both ways, and requires the two results to be identical.
//
// Built with assertions ON and under ASan/UBSan, so a run exercises three
// independent oracles at once - the comparison against std::sort, the
// internal invariants (bucket index in range, leaves tiling [0, n)), and
// the sanitizers' view of the range arithmetic. The whole point of the
// span arithmetic is to be correct where signed overflow would not be, and
// UBSan is what actually holds that claim to account.
//
// The distribution shapes are biased towards the cases that broke things
// historically: keys spanning the entire universe, inputs containing only
// the two extremes, and inputs with almost no distinct values.
//
// Deterministic by construction: a fixed seed, so a failure reproduces.
#include "stratum/StratumSort.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

static std::mt19937_64 rng(0xC0FFEE);
static long long cases = 0;

// One case in four is BOUNDED: a larger n and a workspace whose memory
// budget is drawn below the partner buffer, so the in-place engine runs
// (detail/InPlace.hpp) - with no block buffers, small ones, full ones, or
// full ones plus a partner buffer for the small nodes, depending on the
// draw. Below its floor (the counter arena, tens of KB) a budget is raised
// to it, so n has to be large enough for the partner buffer to exceed it.
static bool drawBounded() { return (rng() & 3) == 0; }

template <typename T>
void oneCase() {
    const bool bounded = drawBounded();
    const std::size_t n = std::uniform_int_distribution<std::size_t>(0, bounded ? 60000 : 3000)(rng);
    const std::size_t lambda = std::uniform_int_distribution<std::size_t>(0, 200)(rng);
    const std::size_t t = std::uniform_int_distribution<std::size_t>(0, 200)(rng);

    // Random distribution shape, biased towards the nasty ones.
    const int shape = std::uniform_int_distribution<int>(0, 9)(rng);
    const T lo = std::numeric_limits<T>::min();
    const T hi = std::numeric_limits<T>::max();
    std::vector<T> v(n);
    std::uniform_int_distribution<long long> full(static_cast<long long>(lo),
                                                  static_cast<long long>(hi));
    for (auto& x : v) {
        switch (shape) {
            case 0: x = static_cast<T>(full(rng)); break;                       // whole universe
            case 1: x = static_cast<T>(std::uniform_int_distribution<int>(0, 3)(rng)); break;
            case 2: x = lo; break;                                              // all equal at min
            case 3: x = (rng() & 1) ? lo : hi; break;                           // both extremes only
            case 4: x = static_cast<T>(full(rng) / 2); break;
            case 5: x = static_cast<T>(std::uniform_int_distribution<int>(-2, 2)(rng)); break;
            default: x = static_cast<T>(full(rng)); break;
        }
    }
    // Shapes 6-9 exercise the presorted paths of 0.11.0: an ascending
    // prefix of random length with a random tail, two sorted runs, a
    // non-increasing input, and an ascending one.
    if (n > 1 && shape >= 6) {
        const std::size_t k = std::uniform_int_distribution<std::size_t>(1, n)(rng);
        switch (shape) {
            case 6: std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k)); break;
            case 7: std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k));
                    std::sort(v.begin() + static_cast<std::ptrdiff_t>(k), v.end()); break;
            case 8: std::sort(v.begin(), v.end()); std::reverse(v.begin(), v.end()); break;
            case 9: std::sort(v.begin(), v.end()); break;
        }
    }
    // Plant the extremes sometimes, so the maximal span is actually hit.
    if (n > 1 && shape < 6 && (rng() & 1)) { v[0] = lo; v[n - 1] = hi; }

    std::vector<T> expected = v;
    std::sort(expected.begin(), expected.end());

    const stratum::StratumSort<T> sorter(lambda, t);
    const std::size_t budget =
        bounded ? std::uniform_int_distribution<std::size_t>(0, n * sizeof(T) + 1)(rng) : stratum::Workspace<T>::kUnlimited;
    stratum::Workspace<T> ws(budget);
    sorter.sort(v, ws);
    ++cases;
    if (v != expected) {
        std::printf("MISMATCH  type=%zub n=%zu lambda=%zu t=%zu shape=%d budget=%zu\n",
                    sizeof(T), n, lambda, t, shape, budget);
        std::exit(1);
    }
}

// Floating point: compared with std::sort under IEEE totalOrder, BIT FOR
// BIT. The comparator uses the library's key map, which tests/float_keys.cpp
// proves equal to totalOrder exhaustively; what this checks is the sorting
// machinery around it, under the sanitizers.
template <typename F, typename Bits>
void oneFloatCase() {
    const bool bounded = drawBounded();
    const std::size_t n = std::uniform_int_distribution<std::size_t>(0, bounded ? 60000 : 3000)(rng);
    std::vector<F> v(n);
    const int shape = std::uniform_int_distribution<int>(0, 3)(rng);
    for (auto& x : v) {
        Bits b = static_cast<Bits>(rng());
        if (shape == 1) b &= static_cast<Bits>(0x8000000000000007ull); // signed zeros, denormals
        if (shape == 2) b |= static_cast<Bits>(~Bits{0} >> 1) & ~static_cast<Bits>(0xF); // NaNs, infinities
        std::memcpy(&x, &b, sizeof x);
    }
    if (shape == 3) std::sort(v.begin(), v.end(), [](F a, F b) {
        return stratum::OrderedKey<F>::key(a) < stratum::OrderedKey<F>::key(b); });
    std::vector<F> expected = v;
    std::sort(expected.begin(), expected.end(), [](F a, F b) {
        return stratum::OrderedKey<F>::key(a) < stratum::OrderedKey<F>::key(b); });
    const std::size_t budget = bounded ? std::uniform_int_distribution<std::size_t>(0, n * sizeof(F) + 1)(rng)
                                       : stratum::Workspace<F>::kUnlimited;
    stratum::Workspace<F> ws(budget);
    if (rng() & 1) stratum::sort(v, ws);
    else stratum::stable_sort(v, ws);
    ++cases;
    if (n != 0 && std::memcmp(v.data(), expected.data(), n * sizeof(F)) != 0) {
        std::printf("MISMATCH  float%zu n=%zu shape=%d budget=%zu\n", sizeof(F) * 8, n, shape, budget);
        std::exit(1);
    }
}

// Records under stable_sort_by_key, compared with std::stable_sort.
void oneRecordCase() {
    struct Row { int32_t key; uint32_t seq; };
    const std::size_t n = std::uniform_int_distribution<std::size_t>(0, 3000)(rng);
    const int distinct = std::uniform_int_distribution<int>(1, 64)(rng);
    std::vector<Row> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = {std::uniform_int_distribution<int>(-distinct, distinct)(rng), static_cast<uint32_t>(i)};
    std::vector<Row> expected = v;
    std::stable_sort(expected.begin(), expected.end(), [](const Row& a, const Row& b) { return a.key < b.key; });
    stratum::Parameters p;
    p.targetElementsPerBin = std::uniform_int_distribution<std::size_t>(0, 200)(rng);
    p.leafThreshold = std::uniform_int_distribution<std::size_t>(0, 200)(rng);
    stratum::Workspace<Row> ws;
    stratum::stable_sort_by_key(v, [](const Row& r) { return r.key; }, ws, p);
    ++cases;
    for (std::size_t i = 0; i < n; ++i) {
        if (v[i].key != expected[i].key || v[i].seq != expected[i].seq) {
            std::printf("MISMATCH  record n=%zu lambda=%zu t=%zu\n", n, p.targetElementsPerBin, p.leafThreshold);
            std::exit(1);
        }
    }
}

int main(int argc, char** argv) {
    const long long iters = argc > 1 ? std::atoll(argv[1]) : 20000;
    for (long long i = 0; i < iters; ++i) {
        switch (i % 11) {
            case 8: oneFloatCase<float, uint32_t>(); break;
            case 9: oneFloatCase<double, uint64_t>(); break;
            case 10: oneRecordCase(); break;
            case 0: oneCase<int8_t>(); break;
            case 1: oneCase<uint8_t>(); break;
            case 2: oneCase<int16_t>(); break;
            case 3: oneCase<uint16_t>(); break;
            case 4: oneCase<int32_t>(); break;
            case 5: oneCase<uint32_t>(); break;
            case 6: oneCase<int64_t>(); break;
            case 7: oneCase<uint64_t>(); break;
        }
    }
    std::printf("OK: %lld cases, no disagreement with std::sort\n", cases);
}
