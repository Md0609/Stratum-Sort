// ============================================================
// PresortedDetection - what it costs to notice that the input is sorted
// ============================================================
// 0.10.0 lost to std::sort on sorted input (6x on the M4, 2x here) because
// it sorted it: analyse, distribute, refine, detect runs leaf by leaf,
// join. Recognising the input first is O(n), and an O(n) check is exactly
// what must not be added blindly - on an input that is NOT sorted it is
// pure overhead. So the question is not whether to detect, but how cheaply.
//
// Three candidates, all measured on the same inputs:
//
//   plain    0.11.0 step 1: one pass for min and max, no detection
//   fused    the same pass also counts descents and ascents, branch-free;
//            sorted <=> no descent, non-increasing <=> no ascent
//   prefix   a separate scan that stops at the first pair breaking the
//            direction set by the first unequal pair, then the plain pass
//
// "prefix" costs almost nothing on random input (it stops within a few
// elements) but a whole extra pass on an input that is sorted for a long
// prefix and then not - sorted_tail, few_outliers. "fused" costs a little
// on every input and never a second pass. The numbers decide.
#include "BenchDatasets.hpp"
#include "stratum/KeyTraits.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Traits = stratum::IntegralKeyTraits<int64_t>;

struct Result {
    uint64_t lo, hi;
    int shape; // 0 unsorted, 1 ascending, 2 non-increasing
};

Result plain(const int64_t* d, std::size_t n) {
    uint64_t lo = Traits::key(d[0]), hi = lo;
    for (std::size_t i = 1; i < n; ++i) {
        const uint64_t k = Traits::key(d[i]);
        lo = k < lo ? k : lo;
        hi = k > hi ? k : hi;
    }
    return {lo, hi, 0};
}

Result fused(const int64_t* d, std::size_t n) {
    uint64_t lo = Traits::key(d[0]), hi = lo, prev = lo;
    std::size_t descents = 0, ascents = 0;
    for (std::size_t i = 1; i < n; ++i) {
        const uint64_t k = Traits::key(d[i]);
        lo = k < lo ? k : lo;
        hi = k > hi ? k : hi;
        descents += k < prev;
        ascents += k > prev;
        prev = k;
    }
    return {lo, hi, descents == 0 ? 1 : ascents == 0 ? 2 : 0};
}

Result prefix(const int64_t* d, std::size_t n) {
    std::size_t i = 1;
    while (i < n && d[i] == d[i - 1]) ++i;
    int shape = 1;
    if (i < n) {
        if (d[i] > d[i - 1]) {
            while (i < n && d[i] >= d[i - 1]) ++i;
            shape = i == n ? 1 : 0;
        } else {
            while (i < n && d[i] <= d[i - 1]) ++i;
            shape = i == n ? 2 : 0;
        }
    }
    if (shape != 0) {
        // The pass above did not track min/max; for a monotone input they
        // are the two ends, which is all the fast path needs.
        return {0, 0, shape};
    }
    Result r = plain(d, n);
    r.shape = 0;
    return r;
}

template <typename F>
double medianMs(F&& f, int reps) {
    std::vector<double> t;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = Clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

volatile uint64_t g_sink = 0;

} // namespace

int main() {
    std::printf("Detection pass alone, median ms of 21 (n = 1e6, int64).\n");
    std::printf("%-15s | %8s %8s %8s | detected\n", "shape", "plain", "fused", "prefix");
    for (const char* shape : {"random", "sorted", "reversed", "nearly_sorted", "few_outliers",
                              "sorted_tail", "duplicates", "organ_pipe"}) {
        const std::vector<int64_t> v = stratum::bench::makeShape<int64_t>(shape, 1000000);
        const std::size_t n = v.size();
        Result rf{}, rp{};
        const double a = medianMs([&] { g_sink += plain(v.data(), n).hi; }, 21);
        const double b = medianMs([&] { rf = fused(v.data(), n); g_sink += rf.hi; }, 21);
        const double c = medianMs([&] { rp = prefix(v.data(), n); g_sink += rp.hi; }, 21);
        const char* names[] = {"unsorted", "ascending", "non-increasing"};
        std::printf("%-15s | %8.3f %8.3f %8.3f | fused: %s, prefix: %s\n", shape, a, b, c,
                    names[rf.shape], names[rp.shape]);
    }
    return 0;
}
