// ============================================================
// Complexity scaling - EXPERIMENTAL EVIDENCE, not proof
// ============================================================
// Deterministic comparison counts against n. Immune to cache and to
// machine load, unlike the clock: if the algorithm carried an n log n
// term, comparisons/n would grow like log n - a factor 1.75 over the
// three decades swept here. It does not. This VALIDATES the proof in
// ALGORITHM.md 8; it does not establish it.
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
    std::printf("Comparison counts vs n. A log n term would raise cmp/n by 1.75x here.\n\n");
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
