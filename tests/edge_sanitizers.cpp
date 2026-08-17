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

// Comprobacion bajo sanitizers de que quitar el recorte de indice es seguro:
// si computeBinIndex() devolviera un indice fuera de rango, la escritura en
// outBucketSize[idx] / dst[writeCursor[b]] seria fuera de limites y ASan lo
// detectaria. Se ejercitan los casos que los tests actuales NO cubren.
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
    // span = 2^64-1 con pocos elementos: el caso donde la anchura (span+1) no
    // es representable y numBuckets == 1.
    all &= ok({lo, hi}, "span=2^64-1, n=2");
    all &= ok(std::vector<int64_t>(64, 0), "todo iguales, n=64");
    { std::vector<int64_t> v(100, lo); v[50] = hi; all &= ok(v, "span=2^64-1, n=100, casi todo min"); }
    { std::vector<int64_t> v; for (int i=0;i<200;++i) v.push_back(i%2 ? hi-i : lo+i);
      all &= ok(v, "span=2^64-1, n=200, alternando extremos"); }
    { std::vector<int64_t> v; for (uint64_t i=0;i<1000;++i) v.push_back((int64_t)((uint64_t)lo + i*(UINT64_MAX/1000)));
      all &= ok(v, "escalon uniforme sobre todo el universo"); }
    std::cout << (all ? "TODO OK\n" : "HAY FALLOS\n");
    return all ? 0 : 1;
}
