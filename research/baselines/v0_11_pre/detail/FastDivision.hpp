#pragma once

#include "../Config.hpp"

// ============================================================
// Exact division by a run-time invariant 64-bit divisor
// ============================================================
// The distribution pass computes floor(offset / width) for every element,
// twice: once to count, once to place. The divisor is fixed for the whole
// pass, so the division can be replaced by a multiplication with a
// precomputed reciprocal, the method of Granlund and Montgomery
// ("Division by Invariant Integers using Multiplication", PLDI 1994) in the
// formulation popularised by libdivide.
//
// THIS IS EXACT, NOT AN APPROXIMATION. For every divisor d >= 1 and every
// 64-bit numerator x, divide(x) == x / d. The partition the algorithm
// builds is therefore bit-for-bit the one the hardware division would
// build, and nothing in the correctness argument or in the Theta(n) proof
// (research/ALGORITHM.md) changes: those are statements about
// floor(offset / width), and this computes exactly that. The claim is
// verified, not only argued - tests/fast_division.cpp compares it with the
// hardware division on every divisor below 2^16, on every power of two
// and its neighbours, and on millions of random pairs, under UBSan.
//
// WHY IT MATTERS FOR MEMORY. 0.10.0 cached each element's bucket in a
// size_t per element, n * 8 bytes, only so that the quotient would not be
// computed twice. With a multiply-high costing a few cycles, recomputing
// is as cheap as reading the cache back - measured in
// research/perf/BucketIndexStrategies.cpp - and the array is gone.
//
// Three shapes, chosen once per divisor:
//   Shift   d is a power of two (including 1):  x >> s
//   Mul     the 64-bit reciprocal is exact:      mulhi(x, m) >> s
//   MulAdd  it needs a 65th bit:                 ((x - q) / 2 + q) >> s
// dispatch() hands the caller the right one as a distinct type, so the hot
// loop is compiled once per shape and contains no branch on it.
// ============================================================

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_ARM64))
#include <intrin.h>
#define STRATUM_HAS_UMULH 1
#endif

namespace stratum_v0_11_pre {
inline namespace STRATUM_ABI_NAMESPACE {
namespace detail {

#if defined(__SIZEOF_INT128__)
// __extension__ keeps -Wpedantic quiet about a non-ISO type that GCC and
// Clang both support on every 64-bit target.
__extension__ typedef unsigned __int128 WideUnsigned;
#define STRATUM_HAS_INT128 1
#endif

// High 64 bits of the 128-bit product a * b, from 32-bit halves. Always
// compiled, so tests/fast_division.cpp can check it on every platform
// against the native form, even where the native form is what runs.
inline uint64_t mulHigh64Portable(uint64_t a, uint64_t b) {
    // Every partial product fits in 64 bits, and 'cross' collects the
    // carries into the high word: it is the sum of three values below
    // 2^32, so it cannot overflow either.
    const uint64_t aLo = a & 0xFFFFFFFFu;
    const uint64_t aHi = a >> 32;
    const uint64_t bLo = b & 0xFFFFFFFFu;
    const uint64_t bHi = b >> 32;
    const uint64_t loLo = aLo * bLo;
    const uint64_t loHi = aLo * bHi;
    const uint64_t hiLo = aHi * bLo;
    const uint64_t hiHi = aHi * bHi;
    const uint64_t cross = (loLo >> 32) + (loHi & 0xFFFFFFFFu) + (hiLo & 0xFFFFFFFFu);
    return hiHi + (loHi >> 32) + (hiLo >> 32) + (cross >> 32);
}

inline uint64_t mulHigh64(uint64_t a, uint64_t b) {
#if defined(STRATUM_HAS_INT128)
    return static_cast<uint64_t>((static_cast<WideUnsigned>(a) * b) >> 64);
#elif defined(STRATUM_HAS_UMULH)
    return __umulh(a, b);
#else
    return mulHigh64Portable(a, b);
#endif
}

// floor((hi * 2^64 + lo) / d) and the remainder, for hi < d (so that the
// quotient fits in 64 bits). Only ever called once per divisor, never per
// element, so the portable form's 64 iterations do not matter.
//
// Restoring binary long division. The running remainder is < d <= 2^64-1,
// so doubling it needs 65 bits; 'carry' is that 65th bit, and when it is
// set the true value exceeds d, so the subtraction is due - computed
// modulo 2^64 it still yields the exact, representable difference.
inline uint64_t divideWidePortable(uint64_t hi, uint64_t lo, uint64_t d, uint64_t& remainder) {
    uint64_t q = 0;
    for (int i = 0; i < 64; ++i) {
        const bool carry = (hi >> 63) != 0;
        hi = (hi << 1) | (lo >> 63);
        lo <<= 1;
        q <<= 1;
        if (carry || hi >= d) {
            hi -= d;
            q |= 1;
        }
    }
    remainder = hi;
    return q;
}

inline uint64_t divideWide(uint64_t hi, uint64_t lo, uint64_t d, uint64_t& remainder) {
#if defined(STRATUM_HAS_INT128)
    const WideUnsigned numerator = (static_cast<WideUnsigned>(hi) << 64) | lo;
    remainder = static_cast<uint64_t>(numerator % d);
    return static_cast<uint64_t>(numerator / d);
#else
    return divideWidePortable(hi, lo, d, remainder);
#endif
}

inline unsigned floorLog2Of64(uint64_t v) {
    unsigned r = 0;
    while (v > 1) {
        v >>= 1;
        ++r;
    }
    return r;
}

class FastDivider64 {
public:
    struct Shift {
        unsigned shift;
        uint64_t operator()(uint64_t x) const { return x >> shift; }
    };
    struct Mul {
        uint64_t magic;
        unsigned shift;
        uint64_t operator()(uint64_t x) const { return mulHigh64(x, magic) >> shift; }
    };
    struct MulAdd {
        uint64_t magic;
        unsigned shift;
        uint64_t operator()(uint64_t x) const {
            const uint64_t q = mulHigh64(x, magic);
            return (((x - q) >> 1) + q) >> shift;
        }
    };

    // Precondition: d >= 1.
    explicit FastDivider64(uint64_t d) {
        const unsigned l = floorLog2Of64(d);
        if ((d & (d - 1)) == 0) {
            kind_ = Kind::Shift;
            shift_ = l;
            return;
        }
        // 2^l < d < 2^(l+1). m = floor(2^(64+l) / d) fits in 64 bits.
        uint64_t rem = 0;
        uint64_t m = divideWide(uint64_t{1} << l, 0, d, rem);
        const uint64_t e = d - rem;
        shift_ = l;
        if (e < (uint64_t{1} << l)) {
            // m + 1 is within 2^-64 relative error, enough for every x.
            kind_ = Kind::Mul;
            magic_ = m + 1;
        } else {
            // Needs 2^(64+l+1)/d, a 65-bit multiplier. Double m and its
            // remainder (the carry out of 2*rem is the 'twiceRem < rem'
            // test), keep the low 64 bits, and let MulAdd supply the
            // implicit top bit.
            m += m;
            const uint64_t twiceRem = rem + rem;
            if (twiceRem >= d || twiceRem < rem) m += 1;
            kind_ = Kind::MulAdd;
            magic_ = m + 1;
        }
    }

    // Calls f with the shape-specific functor; f is instantiated once per
    // shape, so whatever loop it runs is specialised for it.
    template <typename F>
    void dispatch(F&& f) const {
        switch (kind_) {
            case Kind::Shift: f(Shift{shift_}); return;
            case Kind::Mul: f(Mul{magic_, shift_}); return;
            case Kind::MulAdd: f(MulAdd{magic_, shift_}); return;
        }
    }

    // Convenience form for call sites outside a hot loop.
    uint64_t divide(uint64_t x) const {
        switch (kind_) {
            case Kind::Shift: return Shift{shift_}(x);
            case Kind::Mul: return Mul{magic_, shift_}(x);
            case Kind::MulAdd: break;
        }
        return MulAdd{magic_, shift_}(x);
    }

private:
    enum class Kind : unsigned char { Shift, Mul, MulAdd };
    uint64_t magic_ = 0;
    unsigned shift_ = 0;
    Kind kind_ = Kind::Shift;
};

} // namespace detail
} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum_v0_11_pre
