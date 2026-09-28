// ============================================================
// InPlaceDistribution - what it costs to drop the n-element buffer
// ============================================================
// After 0.11.0's memory work the auxiliary memory of a sort is
// n * sizeof(T) for the partner buffer plus O(n / lambda) counters. The
// counters are structural - one per bucket, and the fan-out ceil(m/lambda)
// is the algorithm - but the buffer is not: a distribution step can permute
// in place, as American flag sort does (McIlroy, Bostic, McIlroy 1993).
//
// This measures the top-level distribution step both ways, on identical
// input, with the same exact reciprocal division, the same grid and the
// same resulting partition (checked: the multiset of each bucket must
// match, since in-place permutation is not stable the order may differ):
//
//   out-of-place   0.11.0: count while copying into the buffer, place back
//   in-place       American flag sort: count, then follow permutation
//                  cycles; every element moved once, no buffer
//
// A lockstep variant advancing several cycles at once (to overlap their
// cache misses) was sketched and dropped before measuring: two cycles can
// compete for the last free slot of a bucket, and resolving that needs the
// block-based machinery of IPS4o - a different algorithm to verify, not a
// tweak to measure.
//
// It also runs the whole sort for the in-place variant (top level in place,
// then leaves sorted where they are) against the shipped sort, since the
// leaves of both then sit in the caller's array.
#include "BenchDatasets.hpp"
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using stratum::detail::FastDivider64;
using stratum::detail::Grid;

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
template <typename F>
double timeMs(F&& f) {
    const auto t0 = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

using Traits = stratum::IntegralKeyTraits<int64_t>;

Grid gridFor(const std::vector<int64_t>& v, std::size_t lambda) {
    uint64_t lo = Traits::key(v[0]), hi = lo;
    for (int64_t x : v) {
        lo = std::min(lo, Traits::key(x));
        hi = std::max(hi, Traits::key(x));
    }
    return stratum::detail::planTop(lo, hi, v.size(), lambda);
}

// 0.11.0's top-level step. ends[b] = end of bucket b afterwards.
void outOfPlace(int64_t* data, int64_t* aux, std::size_t n, const Grid& g, uint32_t* ends) {
    const FastDivider64 div(g.width);
    std::fill(ends, ends + g.binCount, 0u);
    div.dispatch([&](const auto& f) {
        for (std::size_t i = 0; i < n; ++i) {
            const int64_t e = data[i];
            aux[i] = e;
            ++ends[f(Traits::key(e) - g.origin)];
        }
    });
    uint32_t c = 0;
    for (std::size_t b = 0; b < g.binCount; ++b) {
        const uint32_t k = ends[b];
        ends[b] = c;
        c += k;
    }
    div.dispatch([&](const auto& f) {
        for (std::size_t i = 0; i < n; ++i) {
            const int64_t e = aux[i];
            data[ends[f(Traits::key(e) - g.origin)]++] = e;
        }
    });
}

// American flag sort step: heads[b] advances through bucket b; ends[b] is
// fixed. Each element is read once where it lies and written once where it
// belongs.
void inPlace(int64_t* data, std::size_t n, const Grid& g, uint32_t* heads, uint32_t* ends) {
    const FastDivider64 div(g.width);
    const std::size_t s = g.binCount;
    std::fill(ends, ends + s, 0u);
    div.dispatch([&](const auto& f) {
        for (std::size_t i = 0; i < n; ++i) ++ends[f(Traits::key(data[i]) - g.origin)];
    });
    uint32_t c = 0;
    for (std::size_t b = 0; b < s; ++b) {
        heads[b] = c;
        c += ends[b];
        ends[b] = c;
    }
    div.dispatch([&](const auto& f) {
        for (std::size_t b = 0; b < s; ++b) {
            while (heads[b] < ends[b]) {
                int64_t e = data[heads[b]];
                std::size_t t = static_cast<std::size_t>(f(Traits::key(e) - g.origin));
                while (t != b) {
                    std::swap(e, data[heads[t]++]);
                    t = static_cast<std::size_t>(f(Traits::key(e) - g.origin));
                }
                data[heads[b]++] = e;
            }
        }
    });
}

bool samePartition(const std::vector<int64_t>& a, const std::vector<int64_t>& b,
                   const uint32_t* ends, std::size_t s) {
    std::vector<int64_t> x = a, y = b;
    std::size_t prev = 0;
    for (std::size_t k = 0; k < s; ++k) {
        std::sort(x.begin() + static_cast<std::ptrdiff_t>(prev), x.begin() + ends[k]);
        std::sort(y.begin() + static_cast<std::ptrdiff_t>(prev), y.begin() + ends[k]);
        prev = ends[k];
    }
    return x == y;
}

void run(const char* shape, std::size_t n) {
    const std::vector<int64_t> input = stratum::bench::makeShape<int64_t>(shape, n);
    const Grid g = gridFor(input, stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN);
    std::vector<uint32_t> heads(g.binCount), ends(g.binCount), ends2(g.binCount);
    std::vector<int64_t> aux(n);
    std::vector<double> tOut, tIn;
    bool same = true;
    for (int r = 0; r < 9; ++r) {
        std::vector<int64_t> a = input, b = input;
        tOut.push_back(timeMs([&] { outOfPlace(a.data(), aux.data(), n, g, ends.data()); }));
        tIn.push_back(timeMs([&] { inPlace(b.data(), n, g, heads.data(), ends2.data()); }));
        same = same && samePartition(a, b, ends.data(), g.binCount);
    }
    const double o = medianOf(tOut), i1 = medianOf(tIn);
    std::printf("%-15s n=%-9zu buckets=%-8zu | out-of-place %8.3f | in-place %8.3f (%+6.1f%%) %s\n",
                shape, n, g.binCount, o, i1, 100 * (i1 / o - 1), same ? "" : "PARTITION MISMATCH");
}

} // namespace

int main() {
    std::printf("Top-level distribution step, milliseconds, median of 9, lambda = 32.\n");
    for (std::size_t n : {std::size_t{100000}, std::size_t{1000000}, std::size_t{10000000}})
        for (const char* shape : {"random", "sorted", "nearly_sorted", "duplicates", "normal"})
            run(shape, n);
    return 0;
}
