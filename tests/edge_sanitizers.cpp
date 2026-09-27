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

// The bucket index is computed without a clamp, on the strength of an
// arithmetic argument rather than a runtime check. This suite exists to
// hold that argument to account: if the index could ever fall out of
// range, the writes to outBucketSize[idx] and dst[writeCursor[b]] would
// be out of bounds and ASan would say so. It exercises the range limits
// the other suites do not reach - spans covering the whole key universe,
// all-equal inputs, and inputs built to exhaust the refinement depth.
#include "stratum/StratumSort.hpp"
#include "DatasetGenerator.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>
using stratum::StratumSort; using stratum::testing::DatasetGenerator;
static bool ok(std::vector<int64_t> v, const char* what) {
    std::vector<int64_t> ref = v; std::sort(ref.begin(), ref.end());
    StratumSort<int64_t> s; s.sort(v);
    const bool good = (v == ref);
    std::cout << "  " << (good ? "PASS " : "FAIL ") << what << "\n";
    return good;
}
int main() {
    bool all = true;
    DatasetGenerator g;
    const int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
    for (std::size_t n : {2u, 63u, 64u, 65u, 129u, 5000u, 50000u}) {
        all &= ok(g.fullRangeExtremes(n), "fullRangeExtremes");
        all &= ok(g.adversarialPeeling(n, 64), "adversarialPeeling");
    }
    // span = 2^64-1 with very few elements: the case where the width
    // (span+1) is not representable and numBuckets == 1.
    all &= ok({lo, hi}, "span=2^64-1, n=2");
    all &= ok(std::vector<int64_t>(64, 0), "all equal, n=64");
    { std::vector<int64_t> v(100, lo); v[50] = hi; all &= ok(v, "span=2^64-1, n=100, almost all min"); }
    { std::vector<int64_t> v; for (int i=0;i<200;++i) v.push_back(i%2 ? hi-i : lo+i);
      all &= ok(v, "span=2^64-1, n=200, alternando extremos"); }
    { std::vector<int64_t> v; for (uint64_t i=0;i<1000;++i) v.push_back((int64_t)((uint64_t)lo + i*(UINT64_MAX/1000)));
      all &= ok(v, "uniform step across the whole universe"); }
    std::cout << (all ? "ALL OK\n" : "FAILURES\n");
    return all ? 0 : 1;
}
