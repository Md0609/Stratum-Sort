#pragma once

// ============================================================
// Key traits - how an element becomes an unsigned 64-bit key
// ============================================================
// Every formula in the algorithm - span, interval width, bucket index - is
// integer arithmetic on the key. The engine never looks at an element
// except through two functions a traits class provides:
//
//   uint64_t key(const Element&)          order-preserving map into uint64_t
//   bool     less(const Element&, const Element&)   the order it preserves
//
// with the contract   less(a, b)  <=>  key(a) < key(b).
//
// That contract is what lets one engine sort several element types without
// knowing anything about them, and it is the only thing a new element type
// has to get right. Two keys that compare equal are equal elements AS FAR
// AS THE SORT IS CONCERNED - which is exactly the question stability
// answers for types where equal keys can be distinguished.
//
// WHY uint64_t AND NOT T. The range arithmetic subtracts the minimum from
// every key. In unsigned arithmetic that subtraction is exact for any two
// keys whose order it preserves; in signed arithmetic it overflows as soon
// as the range straddles zero widely enough. Mapping to unsigned first
// makes the whole engine one piece of unsigned code.
//
// HYPOTHESIS H1 in research/ALGORITHM.md is a statement about this map: the
// keys must fit in w <= 64 bits. It is enforced here, for every traits
// class, by the width of the return type.
// ============================================================

#include <cstdint>
#include <type_traits>

namespace stratum {

// ---- Integral keys -------------------------------------------------
// Unsigned types are their own key. Signed types are sign-extended to
// 64 bits and have the sign bit flipped, which maps
//     INT64_MIN .. -1, 0 .. INT64_MAX   onto   0 .. 2^63-1, 2^63 .. 2^64-1
// monotonically. The difference of two mapped keys equals the difference
// of the values, so every span, width and bucket index is the same number
// 0.10.0 computed with its signed-to-unsigned subtraction; the partition is
// unchanged, bucket for bucket.
template <typename T>
struct IntegralKeyTraits {
    static_assert(std::is_integral<T>::value, "IntegralKeyTraits needs an integral type");
    static_assert(sizeof(T) <= sizeof(uint64_t), "keys wider than 64 bits are not supported");

    using Element = T;

    static uint64_t key(T value) {
        return keyImpl(value, std::is_signed<T>{});
    }
    static bool less(T a, T b) { return a < b; }

private:
    static uint64_t keyImpl(T value, std::true_type /*signed*/) {
        return static_cast<uint64_t>(static_cast<int64_t>(value)) ^ (uint64_t{1} << 63);
    }
    static uint64_t keyImpl(T value, std::false_type /*unsigned*/) {
        return static_cast<uint64_t>(value);
    }
};

} // namespace stratum
