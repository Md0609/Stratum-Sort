// ============================================================
// BucketIndexStrategies - what the per-element bucket index costs
// ============================================================
// 0.10.0 computes each element's bucket in the counting pass, stores it in
// a size_t per element, and reads it back in the placing pass. That array
// is n * 8 bytes - as large as the input for a 64-bit key and eight times
// the input for an 8-bit one - and it exists only to avoid computing the
// same quotient twice.
//
// This measures the alternatives on the distribution step alone, with
// buffers allocated and touched beforehand so that only the two passes are
// timed:
//
//   store64   0.10.0: hardware division, index kept as size_t
//   store32   hardware division, index kept as uint32_t
//   hwdiv x2  hardware division in both passes, nothing kept
//   recip x2  exact reciprocal multiplication in both passes, nothing kept
//
// All four compute exactly floor(offset / width) and produce byte-identical
// output, which the program checks. Median of alternating repetitions.
#include "stratum/detail/FastDivision.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Grid {
    uint64_t origin;
    uint64_t width;
    std::size_t buckets;
};

Grid planFor(const std::vector<uint64_t>& v, std::size_t lambda) {
    const auto mm = std::minmax_element(v.begin(), v.end());
    const uint64_t span = *mm.second - *mm.first;
    Grid g{*mm.first, 1, (v.size() + lambda - 1) / lambda};
    if (span < g.buckets) g.buckets = static_cast<std::size_t>(span + 1);
    g.width = g.buckets == 1 ? 1 : span / g.buckets + 1;
    return g;
}

template <typename Index>
void storeIndex(const std::vector<uint64_t>& in, std::vector<uint64_t>& out, const Grid& g,
                std::vector<Index>& idx, std::vector<std::size_t>& count) {
    const std::size_t n = in.size();
    std::fill(count.begin(), count.begin() + static_cast<std::ptrdiff_t>(g.buckets), 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Index b = static_cast<Index>((in[i] - g.origin) / g.width);
        idx[i] = b;
        ++count[b];
    }
    std::size_t c = 0;
    for (std::size_t b = 0; b < g.buckets; ++b) {
        const std::size_t k = count[b];
        count[b] = c;
        c += k;
    }
    for (std::size_t i = 0; i < n; ++i) out[count[idx[i]]++] = in[i];
}

template <typename Div>
void recompute(const std::vector<uint64_t>& in, std::vector<uint64_t>& out, const Grid& g,
               const Div& div, std::vector<std::size_t>& count) {
    const std::size_t n = in.size();
    const uint64_t origin = g.origin;
    std::fill(count.begin(), count.begin() + static_cast<std::ptrdiff_t>(g.buckets), 0);
    for (std::size_t i = 0; i < n; ++i) ++count[div(in[i] - origin)];
    std::size_t c = 0;
    for (std::size_t b = 0; b < g.buckets; ++b) {
        const std::size_t k = count[b];
        count[b] = c;
        c += k;
    }
    for (std::size_t i = 0; i < n; ++i) out[count[div(in[i] - origin)]++] = in[i];
}

struct HardwareDiv {
    uint64_t d;
    std::size_t operator()(uint64_t x) const { return static_cast<std::size_t>(x / d); }
};

template <typename F>
double timeOnce(F&& f) {
    const auto t0 = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void run(const char* label, std::size_t n, uint64_t valueMask, std::size_t lambda) {
    std::mt19937_64 rng(99);
    std::vector<uint64_t> in(n);
    for (auto& x : in) x = rng() & valueMask;
    const Grid g = planFor(in, lambda);

    std::vector<uint64_t> out(n), ref(n);
    std::vector<uint64_t> idx64(n);
    std::vector<uint32_t> idx32(n);
    std::vector<std::size_t> count(g.buckets);
    const HardwareDiv hw{g.width};
    const stratum::detail::FastDivider64 fast(g.width);

    storeIndex(in, ref, g, idx64, count);
    bool same = true;

    std::vector<double> t64, t32, thw, trc;
    for (int r = 0; r < 11; ++r) {
        t64.push_back(timeOnce([&] { storeIndex(in, out, g, idx64, count); }));
        same = same && out == ref;
        t32.push_back(timeOnce([&] { storeIndex(in, out, g, idx32, count); }));
        same = same && out == ref;
        thw.push_back(timeOnce([&] { recompute(in, out, g, hw, count); }));
        same = same && out == ref;
        trc.push_back(timeOnce([&] { fast.dispatch([&](const auto& div) { recompute(in, out, g, div, count); }); }));
        same = same && out == ref;
    }
    const double base = median(t64);
    std::printf("%-22s n=%-9zu buckets=%-8zu | store64 %7.2f | store32 %7.2f (%+5.1f%%) | "
                "hwdiv x2 %7.2f (%+5.1f%%) | recip x2 %7.2f (%+5.1f%%) %s\n",
                label, n, g.buckets, base, median(t32), 100.0 * (median(t32) / base - 1),
                median(thw), 100.0 * (median(thw) / base - 1), median(trc),
                100.0 * (median(trc) / base - 1), same ? "" : "MISMATCH");
}

} // namespace

int main() {
    std::printf("Distribution step only (count + place), milliseconds, median of 11.\n");
    for (std::size_t n : {std::size_t{100000}, std::size_t{1000000}, std::size_t{10000000}}) {
        run("64-bit span, l=32", n, ~uint64_t{0}, 32);
        run("32-bit span, l=32", n, 0xFFFFFFFFull, 32);
        run("20-bit span, l=32", n, 0xFFFFFull, 32);
        run("64-bit span, l=256", n, ~uint64_t{0}, 256);
    }
    return 0;
}
