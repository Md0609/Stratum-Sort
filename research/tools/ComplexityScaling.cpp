// ============================================================
// Complexity scaling - EXPERIMENTAL EVIDENCE, not proof
// ============================================================
// Deterministic comparison counts against n. Immune to cache and to
// machine load, unlike the clock.
//
// READ THE SCOPE BEFORE USING THESE NUMBERS. The counter is incremented
// only inside the local sorts - detectRun, insertionSort, partition and
// siftDown. analyze() and scanRange() each perform two comparisons per
// element and count none of them, so at least ~18% of the comparison work
// is outside this metric, and the distribution and refinement phases,
// where the (D+1)*n term lives, are invisible to it entirely. A
// superlinearity introduced there would leave these tables flat.
//
// So what follows measures ONE PHASE, not total work. It shows that the
// local-sort phase does not grow with n. It is not evidence for the
// complexity of the algorithm as a whole, and the proof in ALGORITHM.md 8
// does not rest on it: that argument is structural - D <= 6 constant, bin
// count ceil(m/lambda) <= m, O(n) per level, leaves tiling the array,
// quicksort confined to m <= 384, introsort O(m log m).
#include "stratum/StratumSort.hpp"
#include "DatasetGenerator.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using stratum::testing::DataVector;
using stratum::testing::DatasetGenerator;

static double fitExponent(const std::vector<double>& ns, const std::vector<double>& ys) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double k = (double)ns.size();
    for (std::size_t i = 0; i < ns.size(); ++i) {
        const double x = std::log(ns[i]), y = std::log(ys[i]);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    return (k * sxy - sx * sy) / (k * sxx - sx * sx);
}

int main() {
    const std::vector<std::size_t> sizes = {10000, 30000, 100000, 300000, 1000000, 3000000, 10000000};
    struct Case { const char* name; int kind; };
    const std::vector<Case> cases = {
        {"UniformScaled (constant density)", 0},
        {"AdversarialPeeling", 1},
        {"FullRangeExtremes (span 2^64-1)", 2},
    };
    std::printf("Comparison counts vs n, LOCAL-SORT PHASE ONLY.\n"
                "analyze() and scanRange() compare without counting, so this is not\n"
                "total work and must not be read as evidence for overall complexity.\n\n");
    for (const auto& c : cases) {
        std::vector<double> ns, cmp;
        std::printf("%-34s %10s %14s %9s %6s %9s\n", c.name, "n", "comparisons", "cmp/n", "depth", "maxLeaf");
        std::printf("%s\n", std::string(88, '-').c_str());
        for (std::size_t n : sizes) {
            DatasetGenerator g(42);
            DataVector in = c.kind == 0 ? g.randomUniformScaled(n)
                          : c.kind == 1 ? g.adversarialPeeling(n, stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN)
                                        : g.fullRangeExtremes(n);
            stratum::StratumSort<int64_t> s; s.sort(in);
            const auto& m = s.metrics();
            std::printf("%-34s %10zu %14zu %9.2f %6zu %9zu\n", "", n, m.comparisons(),
                        double(m.comparisons()) / double(n), m.maxSubdivisionDepth(), m.maxBinSize());
            ns.push_back((double)n); cmp.push_back((double)m.comparisons());
        }
        std::printf("%-34s => fitted exponent = %.4f  (1.0000 = linear)\n\n", "", fitExponent(ns, cmp));
    }
}
