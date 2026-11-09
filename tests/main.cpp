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
// tests/main.cpp - edge cases and dataset sweep
// ============================================================
// Pure correctness, kept fast on purpose: edge cases plus one
// correctness pass per dataset shape at a moderate size. This binary is
// built with the PRODUCTION configuration (no STRATUM_ENABLE_METRICS, see
// the Makefile) precisely so it validates the same code path a real
// deployment would use - not a research build with instrumentation
// compiled in. Timing and statistics live in their own instrumented
// binaries; this one is meant to run in seconds, not minutes, so it is
// safe to run after every change.
#include "stratum/StratumSort.hpp"
#include "DatasetGenerator.hpp"
#include "Battery.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using stratum::StratumSort;
using stratum::testing::DatasetGenerator;

namespace {

bool checkSorted(std::vector<int64_t> data) {
    std::vector<int64_t> expected = data;
    std::sort(expected.begin(), expected.end());
    StratumSort<int64_t> sorter;
    sorter.sort(data);
    return data == expected;
}

void runEdgeCases() {
    bool ok = true;

    { std::vector<int64_t> v; StratumSort<int64_t> s; s.sort(v); ok &= v.empty(); }
    { std::vector<int64_t> v{42}; StratumSort<int64_t> s; s.sort(v); ok &= (v.size() == 1 && v[0] == 42); }
    ok &= checkSorted({5, 1});
    ok &= checkSorted(std::vector<int64_t>(50, 7));
    ok &= checkSorted({-5, -1, -100, 3, 0, -42});
    ok &= checkSorted({std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 0,
                        std::numeric_limits<int64_t>::min() + 1, std::numeric_limits<int64_t>::max() - 1});

    // A bin whose repeated refine() calls exhaust MAX_SUBDIVISION_DEPTH
    // before shrinking to targetElementsPerBin: many distinct values
    // packed into a narrow range, exercising the depth-limit leaf path.
    {
        DatasetGenerator gen(11);
        std::vector<int64_t> data = gen.smallRangeManyElements(50000);
        ok &= checkSorted(data);
    }

    std::cout << "Edge cases: " << (ok ? "PASS" : "FAIL") << "\n";
    assert(ok);
}

void runDatasetSweep(std::size_t n) {
    DatasetGenerator gen;
    bool ok = true;
    ok &= checkSorted(gen.randomUniform(n));
    ok &= checkSorted(gen.sortedAscending(n));
    ok &= checkSorted(gen.sortedDescending(n));
    ok &= checkSorted(gen.manyRepeated(n));
    ok &= checkSorted(gen.normalDistribution(n));
    ok &= checkSorted(gen.concentrated(n));
    ok &= checkSorted(gen.smallRangeManyElements(n));
    ok &= checkSorted(gen.hugeRangeFewElements(n));
    std::cout << "Dataset sweep (n=" << n << "): " << (ok ? "PASS" : "FAIL") << "\n";
    assert(ok);
}

// The objective.md section 4 battery (datasets/Battery.hpp), every dataset
// in both key types of the objective. Besides sorting each one, pins the
// properties the measuring tools rely on: the ten current datasets are
// BIT-FOR-BIT what DatasetGenerator produced before the battery existed
// (so old and new numbers measure the same inputs), generation is
// deterministic, and only adversary families 7 and 8 may come back
// shorter than requested.
template <typename T>
bool sortsLikeStd(std::vector<T> data) {
    std::vector<T> expected = data;
    std::sort(expected.begin(), expected.end());
    StratumSort<T> sorter;
    sorter.sort(data);
    return data == expected;
}

template <typename T>
bool batterySweepFor(std::size_t n, const char* typeName) {
    using namespace stratum::testing;
    const AdversaryTarget target{StratumSort<T>().targetElementsPerBin(),
                                 stratum::MAX_SUBDIVISION_DEPTH};
    bool ok = true;
    for (std::size_t i = 0; i < battery().size(); ++i) {
        const std::vector<T> v = batteryDataset<T>(i, n, target);
        const bool shortAllowed = i >= 24; // Adv7MaximalLeaf, Adv8TotalCost
        bool here = sortsLikeStd(v) && v == batteryDataset<T>(i, n, target) &&
                    (v.size() == n || (shortAllowed && v.size() <= n));
        if (!here) std::cout << "  battery FAIL: " << battery()[i].name << " " << typeName
                             << " n=" << n << "\n";
        ok &= here;
    }
    return ok;
}

void runBatterySweep() {
    using namespace stratum::testing;
    bool ok = true;
    for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{1000},
                                std::size_t{50000}}) {
        ok &= batterySweepFor<int64_t>(n, "int64");
        ok &= batterySweepFor<uint32_t>(n, "uint32");
    }
    // The current ten, int64_t: unchanged inputs.
    {
        const std::size_t n = 20000;
        const AdversaryTarget target{stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN,
                                     stratum::MAX_SUBDIVISION_DEPTH};
        auto g = [] { return DatasetGenerator(42); };
        ok &= batteryDataset<int64_t>(0, n, target) == g().randomUniform(n);
        ok &= batteryDataset<int64_t>(1, n, target) == g().sortedAscending(n);
        ok &= batteryDataset<int64_t>(2, n, target) == g().sortedDescending(n);
        ok &= batteryDataset<int64_t>(3, n, target) == g().manyRepeated(n);
        ok &= batteryDataset<int64_t>(4, n, target) == g().normalDistribution(n);
        ok &= batteryDataset<int64_t>(5, n, target) == g().concentrated(n);
        ok &= batteryDataset<int64_t>(6, n, target) == g().smallRangeManyElements(n);
        ok &= batteryDataset<int64_t>(7, n, target) == g().hugeRangeFewElements(n);
        ok &= batteryDataset<int64_t>(8, n, target) == g().fullRangeExtremes(n);
        ok &= batteryDataset<int64_t>(9, n, target) ==
              g().adversarialPeeling(n, stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN);
    }
    // Families 7 and 8 must actually be built at the sizes the objective
    // measures (10^5 and up), or the battery would silently lack them.
    {
        const AdversaryTarget target{stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN,
                                     stratum::MAX_SUBDIVISION_DEPTH};
        for (std::size_t i : {std::size_t{24}, std::size_t{25}}) {
            const bool built = batteryDataset<int64_t>(i, 100000, target).size() > 50000 &&
                               batteryDataset<uint32_t>(i, 100000, target).size() > 50000;
            if (!built) std::cout << "  battery FAIL: " << battery()[i].name << " not built at n=1e5\n";
            ok &= built;
        }
    }
    ok &= battery().size() == 26 && batteryIndex("AllEqual") == 17 &&
          batteryIndex("Adv8TotalCost") == 25 && batteryIndex("nope") == battery().size();
    std::cout << "Section 4 battery (26 datasets x int64/uint32): " << (ok ? "PASS" : "FAIL") << "\n";
    assert(ok);
}

} // namespace

int main() {
    runEdgeCases();
    runDatasetSweep(1000);
    runDatasetSweep(50000);
    runBatterySweep();
    std::cout << "All correctness tests passed.\n";
    return 0;
}
