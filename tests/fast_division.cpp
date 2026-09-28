#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// ============================================================
// FastDivider64 must be EXACTLY the hardware division
// ============================================================
// The distribution pass computes bucket = floor(offset / width) with a
// precomputed reciprocal instead of a divide instruction. Every guarantee
// of the algorithm - the index is in range without a clamp, children tile
// their parent, the span contracts, the Theta(n) bound - is a statement
// about floor(offset / width). So the reciprocal is acceptable only if it
// is not an approximation, and this suite is what establishes that:
//
//   1. every divisor d < 2^16 against a battery of numerators, including
//      the ones next to every multiple of d that a wrong magic number gets
//      wrong first (k*d - 1, k*d, k*d + 1) and the top of the range;
//   2. every power of two and its neighbours, 2^k - 2 .. 2^k + 2, which are
//      the boundaries between the three shapes (Shift, Mul, MulAdd);
//   3. the divisors the algorithm actually produces, span / s + 1, for
//      spans reaching 2^64 - 1;
//   4. millions of random (numerator, divisor) pairs with random widths;
//   5. the portable multiply-high and 128/64 division against the native
//      ones, so the fallback that a platform without __int128 or __umulh
//      would run is checked on every platform, not only on that one.
//
// Built under ASan/UBSan in `make sanitizers`, and plainly in `make test`.
#include "stratum/detail/FastDivision.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using stratum::detail::FastDivider64;

namespace {

long long g_checks = 0;
long long g_failures = 0;

void expect(uint64_t x, uint64_t d, const FastDivider64& div) {
    ++g_checks;
    const uint64_t want = x / d;
    const uint64_t got = div.divide(x);
    uint64_t viaDispatch = 0;
    div.dispatch([&](const auto& f) { viaDispatch = f(x); });
    if (got != want || viaDispatch != want) {
        if (++g_failures <= 20) {
            std::printf("  FAIL: %llu / %llu = %llu, got %llu (dispatch %llu)\n",
                        static_cast<unsigned long long>(x), static_cast<unsigned long long>(d),
                        static_cast<unsigned long long>(want), static_cast<unsigned long long>(got),
                        static_cast<unsigned long long>(viaDispatch));
        }
    }
}

void numeratorBattery(uint64_t d, std::mt19937_64& rng) {
    const FastDivider64 div(d);
    const uint64_t top = std::numeric_limits<uint64_t>::max();
    for (uint64_t x : {uint64_t{0}, uint64_t{1}, d - 1, d, d + 1, 2 * d - 1, 2 * d, top, top - 1,
                       top / 2, top / 2 + 1, top - d, top - (top % d), top - (top % d) - 1})
        expect(x, d, div);
    // Next to the largest multiples of d, where the error of a reciprocal
    // that is slightly too small first shows.
    const uint64_t lastMultiple = top - (top % d);
    for (uint64_t k = 0; k < 8 && k * d <= lastMultiple; ++k) {
        const uint64_t m = lastMultiple - k * d;
        expect(m, d, div);
        if (m > 0) expect(m - 1, d, div);
        if (m < top) expect(m + 1, d, div);
    }
    for (int i = 0; i < 16; ++i) expect(rng(), d, div);
    for (int i = 0; i < 8; ++i) expect(rng() >> (rng() % 64), d, div);
}

} // namespace

int main() {
    std::mt19937_64 rng(0x5EED);

    std::printf("1. every divisor below 2^16\n");
    for (uint64_t d = 1; d < (uint64_t{1} << 16); ++d) numeratorBattery(d, rng);

    std::printf("2. powers of two and their neighbours\n");
    for (unsigned k = 1; k < 64; ++k) {
        const uint64_t p = uint64_t{1} << k;
        for (uint64_t d : {p - 2, p - 1, p, p + 1, p + 2})
            if (d >= 1) numeratorBattery(d, rng);
    }
    numeratorBattery(std::numeric_limits<uint64_t>::max(), rng);
    numeratorBattery(std::numeric_limits<uint64_t>::max() - 1, rng);

    std::printf("3. widths the algorithm produces: span / s + 1\n");
    for (int i = 0; i < 20000; ++i) {
        const uint64_t span = (i % 4 == 0) ? std::numeric_limits<uint64_t>::max() : rng() >> (rng() % 64);
        const uint64_t s = 2 + rng() % 1000000;
        if (span < s) continue; // the cap would bind and the width be 1
        const uint64_t width = span / s + 1;
        const FastDivider64 div(width);
        // Every offset in [0, span] must map below s (Property 2), exactly
        // as the hardware division does.
        for (uint64_t x : {uint64_t{0}, span, span / 2, span - 1, rng() % (span + (span < ~uint64_t{0}))}) {
            expect(x, width, div);
            ++g_checks;
            if (div.divide(x) >= s) ++g_failures;
        }
    }

    std::printf("4. random pairs\n");
    for (int i = 0; i < 4000000; ++i) {
        const uint64_t d = (rng() >> (rng() % 64)) | 1u;
        const uint64_t dd = (i & 1) ? d : (d & ~uint64_t{1}) | (rng() & 1);
        if (dd == 0) continue;
        const FastDivider64 div(dd);
        expect(rng() >> (rng() % 64), dd, div);
    }

    std::printf("5. portable arithmetic against the native forms\n");
    for (int i = 0; i < 2000000; ++i) {
        const uint64_t a = rng() >> (rng() % 64);
        const uint64_t b = rng() >> (rng() % 64);
        ++g_checks;
        if (stratum::detail::mulHigh64Portable(a, b) != stratum::detail::mulHigh64(a, b))
            ++g_failures;
        uint64_t d = (rng() >> (rng() % 64)) | 1u;
        const uint64_t hi = rng() % d; // quotient must fit: hi < d
        const uint64_t lo = rng();
        uint64_t r1 = 0, r2 = 0;
        const uint64_t q1 = stratum::detail::divideWidePortable(hi, lo, d, r1);
        const uint64_t q2 = stratum::detail::divideWide(hi, lo, d, r2);
        ++g_checks;
        if (q1 != q2 || r1 != r2 || r1 >= d) ++g_failures;
    }
    for (uint64_t a : {uint64_t{0}, uint64_t{1}, ~uint64_t{0}, uint64_t{1} << 63, (uint64_t{1} << 32) - 1})
        for (uint64_t b : {uint64_t{0}, uint64_t{1}, ~uint64_t{0}, uint64_t{1} << 63, uint64_t{1} << 32}) {
            ++g_checks;
            if (stratum::detail::mulHigh64Portable(a, b) != stratum::detail::mulHigh64(a, b))
                ++g_failures;
        }

    std::printf("%lld checks, %lld failures\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("FastDivider64 agrees with the hardware division everywhere tested.\n");
    return 0;
}
