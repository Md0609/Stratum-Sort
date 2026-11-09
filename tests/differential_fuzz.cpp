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
// Since SS-0 a quarter of the cases are drawn instead from the objective.md
// section 4 battery (datasets/Battery.hpp: the ten current datasets, the
// eight new ones and the eight AdversaryBattery families), at a random size
// and for the case's own key type, with the adversaries rebuilt against the
// lambda the sorter of that case actually uses.
//
// Deterministic by construction: a fixed seed, so a failure reproduces.
#include "Battery.hpp"
#include "stratum/StratumSort.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

static std::mt19937_64 rng(0xC0FFEE);
static long long cases = 0;
static long long batteryCases = 0;

template <typename T>
void oneCase() {
    const std::size_t n = std::uniform_int_distribution<std::size_t>(0, 3000)(rng);
    const std::size_t lambda = std::uniform_int_distribution<std::size_t>(0, 200)(rng);
    const std::size_t t = std::uniform_int_distribution<std::size_t>(0, 200)(rng);

    // Random distribution shape, biased towards the nasty ones. Shapes 6
    // and 7 draw a dataset of the section 4 battery instead.
    const int shape = std::uniform_int_distribution<int>(0, 7)(rng);
    const T lo = std::numeric_limits<T>::min();
    const T hi = std::numeric_limits<T>::max();
    stratum::StratumSort<T> sorter(lambda, t);
    std::size_t dataset = 0;
    std::vector<T> v;
    if (shape >= 6) {
        const auto& b = stratum::testing::battery();
        dataset = std::uniform_int_distribution<std::size_t>(0, b.size() - 1)(rng);
        const stratum::testing::AdversaryTarget target{sorter.targetElementsPerBin(),
                                                       stratum::MAX_SUBDIVISION_DEPTH};
        v = stratum::testing::batteryDataset<T>(dataset, n, target, rng());
        ++batteryCases;
    }
    if (shape < 6) v.resize(n);
    std::uniform_int_distribution<long long> full(static_cast<long long>(lo),
                                                  static_cast<long long>(hi));
    for (auto& x : v) {
        if (shape >= 6) break;
        switch (shape) {
            case 0: x = static_cast<T>(full(rng)); break;                       // whole universe
            case 1: x = static_cast<T>(std::uniform_int_distribution<int>(0, 3)(rng)); break;
            case 2: x = lo; break;                                              // all equal at min
            case 3: x = (rng() & 1) ? lo : hi; break;                           // both extremes only
            case 4: x = static_cast<T>(full(rng) / 2); break;
            case 5: x = static_cast<T>(std::uniform_int_distribution<int>(-2, 2)(rng)); break;
        }
    }
    // Plant the extremes sometimes, so the maximal span is actually hit.
    // Never on a battery dataset: it would destroy the shape being tested.
    if (shape < 6 && n > 1 && (rng() & 1)) { v[0] = lo; v[n - 1] = hi; }

    std::vector<T> expected = v;
    std::sort(expected.begin(), expected.end());

    sorter.sort(v);
    ++cases;
    if (v != expected) {
        std::printf("MISMATCH  type=%zub n=%zu lambda=%zu t=%zu shape=%d dataset=%s\n",
                    sizeof(T), v.size(), lambda, t, shape,
                    shape >= 6 ? stratum::testing::battery()[dataset].name : "-");
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    const long long iters = argc > 1 ? std::atoll(argv[1]) : 20000;
    for (long long i = 0; i < iters; ++i) {
        switch (i % 8) {
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
    std::printf("OK: %lld cases (%lld from the section 4 battery), no disagreement with std::sort\n",
                cases, batteryCases);
}
