// ============================================================
// DRS v7 - tests/main.cpp
// ============================================================
// Pure correctness, kept fast on purpose: edge cases plus one
// correctness pass per dataset shape at a moderate size. This binary is
// built with the PRODUCTION configuration (no DRS_ENABLE_METRICS, see
// the Makefile) precisely so it validates the same code path a real
// deployment would use - not a research build with instrumentation
// compiled in. Timing, statistics and research experiments live in
// benchmarks/, analisis/ and experimentos/ (all research-configured);
// this binary is meant to run in seconds, not minutes, so it is safe to
// run after every change.
#include "DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using drs::DynamicRangeSort;
using drs::testing::DatasetGenerator;

namespace {

bool checkSorted(std::vector<int64_t> data) {
    std::vector<int64_t> expected = data;
    std::sort(expected.begin(), expected.end());
    DynamicRangeSort<int64_t> sorter;
    sorter.sort(data);
    return data == expected;
}

void runEdgeCases() {
    bool ok = true;

    { std::vector<int64_t> v; DynamicRangeSort<int64_t> s; s.sort(v); ok &= v.empty(); }
    { std::vector<int64_t> v{42}; DynamicRangeSort<int64_t> s; s.sort(v); ok &= (v.size() == 1 && v[0] == 42); }
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

} // namespace

int main() {
    runEdgeCases();
    runDatasetSweep(1000);
    runDatasetSweep(50000);
    std::cout << "All correctness tests passed.\n";
    return 0;
}
