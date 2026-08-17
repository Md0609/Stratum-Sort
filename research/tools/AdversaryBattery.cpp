// ============================================================
// Adversary battery - EXPERIMENTAL EVIDENCE, not proof
// ============================================================
// Validates the residual bound B(n) of ALGORITHM.md 8.4 against the
// implementation, using deterministic counters only. A measurement can
// refute the theorem; it can never establish it.
//
//   B(n) = min(n, (lambda^(D+1) * 2^w / n)^(1/D))
//   M    = sup B(n) = lambda * 2^(w/(D+1))
//
// For each family it reports the largest leaf with span > 0 that reaches a
// comparison sort, and comparisons per element. If any leaf exceeds B(n),
// the theorem is refuted and this program says so.
#include "stratum/StratumSort.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>
using u64 = unsigned long long;
using Vec = std::vector<int64_t>;
static const u64 L = stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN;
static const u64 D = stratum::MAX_SUBDIVISION_DEPTH;
static u64 ipow(u64 s, u64 k) { u64 r = 1; for (u64 i = 0; i < k; ++i) r *= s; return r; }
static bool fits(u64 s, u64 k, u64 lim) { u64 r = 1; for (u64 i = 0; i < k; ++i) { if (r > lim / s) return false; r *= s; } return true; }
static double Bn(u64 n) {
    return std::min((double)n, std::pow(std::pow((double)L, (double)D + 1) * std::pow(2.0, 64) / (double)n, 1.0 / (double)D));
}
static int64_t offv(u64 off) { return (int64_t)((u64)std::numeric_limits<int64_t>::min() + off); }

// The family that saturates the bound: core of s*L-D elements in a span of
// 1, one singleton peeled per level, filler placed so the top-level bucket
// is exactly wide enough to hold the group.
static Vec maximalLeaf(u64 n) {
    const u64 s_top = (n + L - 1) / L, lim = (~u64(0)) / s_top;
    u64 s = 2; while (fits(s + 1, D, lim)) ++s;
    const u64 C = s * L - D;
    Vec v; if (C + D > n) return v;
    v.reserve(n);
    for (u64 i = 0; i < C; ++i) v.push_back(offv(i % 2 == 0 ? 1 : 0));
    for (u64 k = 1; k <= D; ++k) v.push_back(offv(ipow(s, k)));
    while (v.size() < n) v.push_back(offv(s_top * ipow(s, D)));
    return v;
}
// The same idea replicated k times, so nearly every element ends up in an
// expensive leaf. This is the family that maximises TOTAL cost.
static Vec totalCost(u64 nT) {
    u64 s = 2; while (fits(s + 1, D, ((~u64(0)) / 2 / nT) * L)) ++s;
    const u64 k = nT / (s * L); if (k < 2) return {};
    const u64 C = s * L - D, stride = 2 * ipow(s, D + 1);
    Vec v; v.reserve(k * s * L);
    for (u64 g = 0; g < k; ++g) {
        const u64 b = g * stride;
        for (u64 i = 0; i < C; ++i) v.push_back(offv(b + (i % 2 == 0 ? 1 : 0)));
        for (u64 q = 1; q <= D; ++q) v.push_back(offv(b + ipow(s, q)));
    }
    return v;
}

struct Result { std::size_t worstLeaf; double cmpPerN; std::size_t depth; unsigned intro, quick; bool ok; };

static Result run(Vec v, std::size_t lambda, std::size_t t) {
    Vec ref = v; std::sort(ref.begin(), ref.end());
    stratum::StratumSort<int64_t> probe(lambda, t);
    auto leaves = probe.debugPartitionOnly(v);
    const auto& A = probe.debugBufferA(); const auto& B = probe.debugBufferB();
    std::size_t worst = 0;
    for (const auto& lf : leaves) {
        if (lf.count < 2) continue;
        const auto& buf = lf.inBufferA ? A : B;
        auto mm = std::minmax_element(buf.begin() + lf.start, buf.begin() + lf.start + lf.count);
        if (*mm.first != *mm.second && lf.count > worst) worst = lf.count;
    }
    stratum::StratumSort<int64_t> s(lambda, t); s.sort(v);
    const auto& m = s.metrics();
    auto use = [&](const char* a) { auto it = m.algorithmUsage().find(a); return it == m.algorithmUsage().end() ? 0u : (unsigned)it->second; };
    return {worst, v.empty() ? 0.0 : double(m.comparisons()) / double(v.size()),
            m.maxSubdivisionDepth(), use("Introsort"), use("QuickSort"), v == ref};
}

int main() {
    std::printf("lambda=%llu t=%llu D=%llu w=64   M = lambda*2^(w/(D+1)) = %.1f\n\n",
                L, (u64)stratum::DEFAULT_LEAF_THRESHOLD, D, (double)L * std::pow(2.0, 64.0 / (D + 1)));
    const std::vector<u64> sizes = {100000, 300000, 1000000, 3000000};
    std::mt19937_64 rng(20260817);

    std::printf("%-26s %9s %11s %11s %8s %8s %6s %5s\n",
                "family", "n", "worstLeaf", "B(n)", "<=B(n)", "cmp/n", "depth", "ok");
    std::printf("%s\n", std::string(96, '-').c_str());

    bool refuted = false;
    for (u64 n : sizes) {
        struct Case { const char* name; Vec v; };
        std::vector<Case> cases;
        { Vec v(n, 42); cases.push_back({"1 all equal", v}); }
        { Vec v(n); for (u64 i = 0; i < n; ++i) v[i] = (int64_t)i; std::shuffle(v.begin(), v.end(), rng);
          cases.push_back({"2 all distinct", v}); }
        { Vec v(n); for (auto& x : v) x = (int64_t)(rng() % 8); cases.push_back({"3 massive duplicates", v}); }
        { Vec v(n, 1000); for (u64 i = 0; i < n / 1000; ++i) v[i * 1000] = (int64_t)rng();
          std::shuffle(v.begin(), v.end(), rng); cases.push_back({"4 dominant + outliers", v}); }
        { Vec v(n); for (u64 i = 0; i < n; ++i) v[i] = (int64_t)(i % 2); cases.push_back({"5 alternating", v}); }
        { Vec v(n); for (u64 i = 0; i < n; ++i) v[i] = (i < n - 1) ? (int64_t)(rng() % 4) : (int64_t)1e18;
          cases.push_back({"6 one bucket + far", v}); }
        { Vec v = maximalLeaf(n); if (!v.empty()) cases.push_back({"7 maximal leaf", v}); }
        { Vec v = totalCost(n); if (!v.empty()) cases.push_back({"8 total cost", v}); }

        for (auto& c : cases) {
            const u64 realN = c.v.size();
            Result r = run(std::move(c.v), 0, 0);
            const double bound = Bn(realN);
            const bool within = (double)r.worstLeaf <= bound;
            if (!within) refuted = true;
            std::printf("%-26s %9llu %11zu %11.1f %8s %8.2f %6zu %5s\n",
                        c.name, realN, r.worstLeaf, bound, within ? "yes" : "*** NO",
                        r.cmpPerN, r.depth, r.ok ? "yes" : "NO");
        }
        std::printf("\n");
    }

    // 9  n around n* = M, where B(n) stops being vacuous
    std::printf("-- 9  n around n* = M ~ 18090 --------------------------------\n");
    for (u64 n : {u64(4000), u64(18090), u64(60000)}) {
        Vec v = maximalLeaf(n); if (v.empty()) continue;
        Result r = run(std::move(v), 0, 0);
        const bool w = (double)r.worstLeaf <= Bn(n); if (!w) refuted = true;
        std::printf("%-26s %9llu %11zu %11.1f %8s %8.2f\n", "maximal leaf", n, r.worstLeaf, Bn(n), w ? "yes" : "*** NO", r.cmpPerN);
    }

    // 11/12  lambda and t at their limits, on the hardest family
    std::printf("\n-- 11/12  lambda and t at the limits (n=1e6, family 8) -------\n");
    std::printf("%8s %8s %11s %8s %6s %5s\n", "lambda", "t", "worstLeaf", "cmp/n", "depth", "ok");
    for (auto pr : std::vector<std::pair<std::size_t, std::size_t>>{
             {1, 1}, {1, 64}, {2, 2}, {32, 32}, {32, 64}, {64, 64}, {256, 256}, {1024, 1024}, {32, 4096}, {0, 0}}) {
        Vec v = totalCost(1000000); if (v.empty()) continue;
        Result r = run(std::move(v), pr.first, pr.second);
        std::printf("%8zu %8zu %11zu %8.2f %6zu %5s\n", pr.first, pr.second, r.worstLeaf, r.cmpPerN, r.depth, r.ok ? "yes" : "NO");
    }

    std::printf("\n%s\n", refuted ? "*** A LEAF EXCEEDED B(n): THE THEOREM IS REFUTED ***"
                                  : "No leaf exceeded B(n) in any family. Consistent with the theorem; not a proof of it.");
    return refuted ? 1 : 0;
}
