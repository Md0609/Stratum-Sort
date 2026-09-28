#pragma once

#include "Config.hpp"

// ============================================================
// Key traits - how an element becomes an unsigned 64-bit key
// ============================================================
// Every formula in the algorithm - span, interval width, bucket index - is
// integer arithmetic on the key. The engine never looks at an element
// except through two functions a traits object provides:
//
//   uint64_t key(const Element&)                    order-preserving map
//   bool     less(const Element&, const Element&)   the order it preserves
//
// with the contract   less(a, b)  <=>  key(a) < key(b).
//
// That contract is what lets one engine sort several element types without
// knowing anything about them, and it is the only thing a new element type
// has to get right. Two elements whose keys are equal are equivalent AS FAR
// AS THE SORT IS CONCERNED - which is exactly the question stability
// answers, for element types where equivalent elements can be told apart.
//
// WHY uint64_t. The range arithmetic subtracts the minimum from every key.
// In unsigned arithmetic that subtraction is exact for any two keys whose
// order it preserves; in signed arithmetic it overflows as soon as the
// range straddles zero widely enough. Mapping to unsigned first makes the
// whole engine one piece of unsigned code.
//
// HYPOTHESIS H1 in research/ALGORITHM.md is a statement about this map: the
// keys must fit in w <= 64 bits. It holds for every key type below by the
// width of the return type, and every one of them is static_assert'd.
//
// SUPPORTED KEY TYPES (OrderedKey<K>):
//   - every integral type except bool, signed or unsigned, of at most 64
//     bits: char, signed/unsigned char, char16_t, char32_t, wchar_t,
//     short ... long long, and their aliases (int64_t, size_t, ...);
//   - enumerations, scoped or not, through their underlying type;
//   - float and double, when they are IEEE-754 binary32/binary64, in the
//     IEEE-754 totalOrder (see OrderedKey<float> below).
// A wrapper type (struct Id { uint32_t v; }) is sorted with sort_by_key
// and an extractor returning the wrapped integer. Strings, long double and
// 128-bit integers are not supported: their keys do not fit in 64 bits,
// and H1 is what bounds the depth of the recursion.
// ============================================================

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

namespace detail {

template <typename K, typename Enable = void>
struct OrderedKeyImpl {
    static constexpr bool supported = false;
};

// ---- Integral keys -------------------------------------------------
// Unsigned types are their own key. Signed types are sign-extended to
// 64 bits and have the sign bit flipped, which maps
//     INT64_MIN .. -1, 0 .. INT64_MAX   onto   0 .. 2^63-1, 2^63 .. 2^64-1
// monotonically. The difference of two mapped keys equals the difference
// of the values, so every span, width and bucket index is the number 0.10.0
// computed with its signed-to-unsigned subtraction.
template <typename K>
struct OrderedKeyImpl<K, typename std::enable_if<std::is_integral<K>::value &&
                                                 !std::is_same<K, bool>::value>::type> {
    static_assert(sizeof(K) <= sizeof(uint64_t),
                  "keys wider than 64 bits are not supported (hypothesis H1)");
    static constexpr bool supported = true;
    static uint64_t key(K value) { return keyImpl(value, std::is_signed<K>{}); }
    static bool less(K a, K b) { return a < b; }
    // The inverse of key(): exact for every key key() can produce.
    static K fromKey(uint64_t k) { return fromKeyImpl(k, std::is_signed<K>{}); }

private:
    static uint64_t keyImpl(K value, std::true_type) {
        return static_cast<uint64_t>(static_cast<int64_t>(value)) ^ (uint64_t{1} << 63);
    }
    static uint64_t keyImpl(K value, std::false_type) { return static_cast<uint64_t>(value); }
    static K fromKeyImpl(uint64_t k, std::true_type) {
        return static_cast<K>(static_cast<int64_t>(k ^ (uint64_t{1} << 63)));
    }
    static K fromKeyImpl(uint64_t k, std::false_type) { return static_cast<K>(k); }
};

// ---- Enumerations --------------------------------------------------
// Ordered as their underlying integer, which is what operator< does for
// an unscoped enum and what a scoped enum's values mean.
template <typename K>
struct OrderedKeyImpl<K, typename std::enable_if<std::is_enum<K>::value>::type> {
    using Underlying = typename std::underlying_type<K>::type;
    static constexpr bool supported = OrderedKeyImpl<Underlying>::supported;
    static uint64_t key(K value) {
        return OrderedKeyImpl<Underlying>::key(static_cast<Underlying>(value));
    }
    static bool less(K a, K b) { return static_cast<Underlying>(a) < static_cast<Underlying>(b); }
    static K fromKey(uint64_t k) { return static_cast<K>(OrderedKeyImpl<Underlying>::fromKey(k)); }
};

// ---- IEEE-754 floating point --------------------------------------
// The key of x is its bit pattern with
//     negative (sign bit set):   every bit inverted
//     positive (sign bit clear): the sign bit set
// which orders the keys, as unsigned integers, exactly as
//
//     -NaN  <  -inf  <  negative  <  -0  <  +0  <  positive  <  +inf  <  +NaN
//
// with NaNs ordered by payload (reversed for negative ones): that is the
// totalOrder predicate of IEEE 754-2008, section 5.10. It is a strict weak
// order - a total one - so a sort by it is well defined, which a sort by
// operator< is not once a NaN is present.
//
// Relation to operator<. On inputs without NaN, operator< treats -0 and +0
// as equivalent and totalOrder puts -0 first; every output of this sort is
// therefore also a valid output of std::sort with operator<. With NaNs,
// std::sort's behaviour is undefined; this puts negative NaNs first and
// positive NaNs last.
//
// The map is a bijection on bit patterns (decode() inverts it), so a sort
// is a permutation of the input's exact bits: -0 stays -0, NaN payloads
// survive. PROVED, not argued: tests/float_keys.cpp walks all 2^32 float
// patterns in key order and checks the sequence against an independent
// totalOrder written from the standard's definition.
template <typename F, typename Bits>
struct FloatKey {
    static_assert(std::numeric_limits<F>::is_iec559, "float keys need IEEE-754 arithmetic");
    static_assert(sizeof(F) == sizeof(Bits), "unexpected floating-point width");
    static constexpr bool supported = true;
    static constexpr unsigned kTop = 8 * sizeof(Bits) - 1;
    static constexpr Bits kSign = Bits{1} << kTop;

    // Both directions are written as one XOR with a mask derived from the
    // sign bit, never as a conditional. The two are equal on every bit
    // pattern, but a conditional is compiled to a BRANCH by MSVC, and the
    // sign of random data is a coin flip: on the Windows runner that
    // misprediction made float keys cost 1.9x what int64 keys cost, and
    // lose to std::sort on inputs with few distinct values. GCC and Clang
    // were already branch-free; for them this is 0-15% faster.
    //   encode: sign set   -> mask all ones  -> ~b
    //           sign clear -> mask = kSign   -> b | kSign
    static Bits encode(F value) {
        Bits b;
        std::memcpy(&b, &value, sizeof b);
        return static_cast<Bits>(b ^ (static_cast<Bits>(Bits{0} - (b >> kTop)) | kSign));
    }
    //   decode: key top set   -> mask = kSign  -> k ^ kSign
    //           key top clear -> mask all ones -> ~k
    static F decode(Bits k) {
        const Bits b = static_cast<Bits>(k ^ (static_cast<Bits>((k >> kTop) - Bits{1}) | kSign));
        F value;
        std::memcpy(&value, &b, sizeof value);
        return value;
    }
    static uint64_t key(F value) { return static_cast<uint64_t>(encode(value)); }
    static bool less(F a, F b) { return encode(a) < encode(b); }
    static F fromKey(uint64_t k) { return decode(static_cast<Bits>(k)); }
};

template <>
struct OrderedKeyImpl<float> : FloatKey<float, uint32_t> {};
template <>
struct OrderedKeyImpl<double> : FloatKey<double, uint64_t> {};

} // namespace detail

// OrderedKey<K>::supported tells whether K can be a key; key() and less()
// exist only when it can.
template <typename K>
struct OrderedKey : detail::OrderedKeyImpl<typename std::remove_cv<K>::type> {};

// ---- Elements that are their own key ------------------------------
template <typename T>
struct SelfKeyTraits {
    static_assert(OrderedKey<T>::supported,
                  "not a supported key type: integral (not bool), enum, float or double");
    using Element = T;
    static uint64_t key(const T& value) { return OrderedKey<T>::key(value); }
    static bool less(const T& a, const T& b) { return OrderedKey<T>::less(a, b); }

    // The element IS its key: two elements with equal keys are the same
    // bit pattern, so an element can be rebuilt from its key. That is
    // what lets the engine finish a node whose every bucket holds a single
    // key by counting instead of moving (Engine.hpp, countingFill()).
    static constexpr bool kElementIsKey = true;
    static T fromKey(uint64_t k) { return OrderedKey<T>::fromKey(k); }
};

// The traits 0.10.0's integral keys use. Kept under its own name so that
// StratumSort<T> documents, in its type, that it is the integer sorter.
template <typename T>
struct IntegralKeyTraits : SelfKeyTraits<T> {
    static_assert(std::is_integral<T>::value, "IntegralKeyTraits needs an integral type");
};

// ---- Elements sorted by an extracted key --------------------------
// KeyFn is called as key(element) and must return a supported key type.
// It is called several times per element and must return the same key
// every time, must not modify the element, and must not throw - the strong
// exception guarantee assumes nothing after the first write can.
template <typename E, typename KeyFn>
struct ExtractedKeyTraits {
    using Element = E;
    using KeyType = typename std::decay<decltype(std::declval<const KeyFn&>()(std::declval<const E&>()))>::type;
    static_assert(OrderedKey<KeyType>::supported,
                  "the key extractor must return an integral (not bool), enum, float or double "
                  "key");

    KeyFn fn;

    // Equal keys do not make equal records: the payload differs.
    static constexpr bool kElementIsKey = false;

    uint64_t key(const E& e) const { return OrderedKey<KeyType>::key(fn(e)); }
    bool less(const E& a, const E& b) const { return OrderedKey<KeyType>::less(fn(a), fn(b)); }
};

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
