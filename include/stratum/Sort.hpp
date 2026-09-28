#pragma once

// Included by StratumSort.hpp; can also be included on its own.

// ============================================================
// Free functions: the stateless API
// ============================================================
// StratumSort<T> is 0.10.0's interface - an object that holds settings and
// scratch - and it is unchanged. These functions are the rest of 0.11.0:
//
//   sort(v) / stable_sort(v)
//       keys that are their own value: integral (not bool), enum, float,
//       double. float and double sort in IEEE-754 totalOrder (KeyTraits.hpp).
//
//   sort_by_key(v, key) / stable_sort_by_key(v, key)
//       records sorted by an extracted key, the record moving as a whole:
//           struct Row { uint64_t id; Payload p; };
//           stratum::stable_sort_by_key(rows, [](const Row& r) { return r.id; });
//       The record type must be trivially copyable (it is moved with memcpy).
//
//   sorted_indices(first, last, key)
//       the permutation that sorts a range by key, stably, for ANY element
//       type - including ones that are not trivially copyable, or too large
//       to be worth moving through the distribution passes.
//
// Every function has a form taking a Workspace, to reuse scratch across
// calls and to sort on several threads with one workspace per thread, and a
// Parameters argument whose fields are named - so lambda and t cannot be
// swapped by accident, the hazard 0.10.0's constructor documents.
//
// STABILITY, precisely. The stable_* functions keep elements with equal
// keys in their original order, for every input, and tests/stability.cpp
// holds them to it with identifiable payloads. The unstable ones make no
// promise about equal keys - they are deterministic, but a different
// lambda can order equal keys differently. For a key that is the whole
// element (sort, stable_sort on integers, enums, floats) the two are
// indistinguishable: equal keys are equal bit patterns.
//
// COMPLEXITY. All of them are the same engine, so Theta(n) worst case under
// the hypotheses of StratumSort.hpp, with the key's width as w. The stable
// variant replaces the leaves' introsort by a merge sort, O(m log m) on a
// leaf of m <= max(t, M) elements exactly like introsort, so the bound and
// its proof are unchanged (research/ALGORITHM.md, section 8.6).
// ============================================================

#include "Config.hpp"
#include "KeyTraits.hpp"
#include "Workspace.hpp"
#include "detail/Engine.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

namespace detail {

template <bool Stable, typename Traits>
void run(const Traits& traits, typename Traits::Element* first, std::size_t n,
         Workspace<typename Traits::Element>& workspace, const Parameters& p) {
    const Parameters c = resolveParameters(p, n);
#ifdef STRATUM_ENABLE_METRICS
    workspace.metrics().reset();
    const Probe probe(&workspace.metrics());
#else
    const Probe probe;
#endif
    SortLeaves sink;
    sortWith<Stable>(traits, first, n, c.targetElementsPerBin, c.leafThreshold, workspace, probe,
                     sink);
}

template <typename E>
void requireMovable() {
    static_assert(std::is_trivially_copyable<E>::value,
                  "the element type must be trivially copyable: elements are moved with memcpy. "
                  "Use stratum::sorted_indices for other types.");
}

} // namespace detail

// ---- Keys that are their own value -------------------------------------
template <typename T>
void sort(T* first, T* last, Workspace<T>& workspace, const Parameters& p = {}) {
    assert(first <= last);
    detail::run<false>(SelfKeyTraits<T>{}, first, static_cast<std::size_t>(last - first), workspace, p);
}
template <typename T>
void sort(T* first, T* last) {
    Workspace<T> workspace;
    sort(first, last, workspace);
}
template <typename T>
void sort(std::vector<T>& v, Workspace<T>& workspace, const Parameters& p = {}) {
    sort(v.data(), v.data() + v.size(), workspace, p);
}
template <typename T>
void sort(std::vector<T>& v) {
    Workspace<T> workspace;
    sort(v, workspace);
}

template <typename T>
void stable_sort(T* first, T* last, Workspace<T>& workspace, const Parameters& p = {}) {
    assert(first <= last);
    detail::run<true>(SelfKeyTraits<T>{}, first, static_cast<std::size_t>(last - first), workspace, p);
}
template <typename T>
void stable_sort(T* first, T* last) {
    Workspace<T> workspace;
    stable_sort(first, last, workspace);
}
template <typename T>
void stable_sort(std::vector<T>& v, Workspace<T>& workspace, const Parameters& p = {}) {
    stable_sort(v.data(), v.data() + v.size(), workspace, p);
}
template <typename T>
void stable_sort(std::vector<T>& v) {
    Workspace<T> workspace;
    stable_sort(v, workspace);
}

// ---- Records by an extracted key ----------------------------------------
template <typename E, typename KeyFn>
void sort_by_key(E* first, E* last, KeyFn key, Workspace<E>& workspace, const Parameters& p = {}) {
    detail::requireMovable<E>();
    assert(first <= last);
    detail::run<false>(ExtractedKeyTraits<E, KeyFn>{std::move(key)}, first,
                       static_cast<std::size_t>(last - first), workspace, p);
}
template <typename E, typename KeyFn>
void sort_by_key(E* first, E* last, KeyFn key) {
    Workspace<E> workspace;
    sort_by_key(first, last, std::move(key), workspace);
}
template <typename E, typename KeyFn>
void sort_by_key(std::vector<E>& v, KeyFn key, Workspace<E>& workspace, const Parameters& p = {}) {
    sort_by_key(v.data(), v.data() + v.size(), std::move(key), workspace, p);
}
template <typename E, typename KeyFn>
void sort_by_key(std::vector<E>& v, KeyFn key) {
    Workspace<E> workspace;
    sort_by_key(v, std::move(key), workspace);
}

template <typename E, typename KeyFn>
void stable_sort_by_key(E* first, E* last, KeyFn key, Workspace<E>& workspace,
                        const Parameters& p = {}) {
    detail::requireMovable<E>();
    assert(first <= last);
    detail::run<true>(ExtractedKeyTraits<E, KeyFn>{std::move(key)}, first,
                      static_cast<std::size_t>(last - first), workspace, p);
}
template <typename E, typename KeyFn>
void stable_sort_by_key(E* first, E* last, KeyFn key) {
    Workspace<E> workspace;
    stable_sort_by_key(first, last, std::move(key), workspace);
}
template <typename E, typename KeyFn>
void stable_sort_by_key(std::vector<E>& v, KeyFn key, Workspace<E>& workspace,
                        const Parameters& p = {}) {
    stable_sort_by_key(v.data(), v.data() + v.size(), std::move(key), workspace, p);
}
template <typename E, typename KeyFn>
void stable_sort_by_key(std::vector<E>& v, KeyFn key) {
    Workspace<E> workspace;
    stable_sort_by_key(v, std::move(key), workspace);
}

// ---- Indirect sorting ------------------------------------------------------
// One (key, position) pair per element, sorted stably by key with the same
// engine; returns the positions in sorted order. Works for any element type
// and never moves an element. For a trivially copyable record,
// research/perf/RecordStrategies.cpp measures when this beats moving the
// records themselves (sort_by_key).
namespace detail {
struct IndexedKey {
    uint64_t key;
    std::size_t index;
};
struct IndexedKeyTraits {
    using Element = IndexedKey;
    static constexpr bool kElementIsKey = false; // the index is a payload
    static uint64_t key(const IndexedKey& e) { return e.key; }
    static bool less(const IndexedKey& a, const IndexedKey& b) { return a.key < b.key; }
};
} // namespace detail

template <typename It, typename KeyFn>
std::vector<std::size_t> sorted_indices(It first, It last, KeyFn key) {
    using KeyType = typename std::decay<decltype(key(*first))>::type;
    static_assert(OrderedKey<KeyType>::supported,
                  "the key extractor must return an integral (not bool), enum, float or double key");
    std::vector<detail::IndexedKey> pairs;
    for (std::size_t i = 0; first != last; ++first, ++i)
        pairs.push_back({OrderedKey<KeyType>::key(key(*first)), i});
    Workspace<detail::IndexedKey> workspace;
    detail::run<true>(detail::IndexedKeyTraits{}, pairs.data(), pairs.size(), workspace, Parameters{});
    std::vector<std::size_t> order(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) order[i] = pairs[i].index;
    return order;
}

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
