#pragma once

#include "Config.hpp"
#include "KeyTraits.hpp"
#include "Workspace.hpp"
#include "detail/Engine.hpp"

#ifdef STRATUM_ENABLE_METRICS
#include "Metrics.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

// ============================================================
// StratumSort
// ============================================================
// A distribution sort for integer keys.
//
// Rather than comparing elements against each other globally, it looks at
// how the values are spread out, cuts the observed value range into
// equal-width intervals, refines the intervals that are still too crowded
// using only that interval's own observed range, sorts each final
// interval locally, and concatenates the result.
//
//     analyse -> build intervals -> refine -> sort locally -> join
//
// The join is a plain concatenation, never a merge: intervals are
// value-ordered by construction, so every element of interval j is <=
// every element of interval j+1.
//
// ---- Complexity ---------------------------------------------------
// Theta(n) worst-case time and Theta(n) auxiliary space, for every input
// and every constructor argument, treating the key width w as a
// constant - the same sense in which radix sort is linear. This is NOT
// a bound in the comparison model and does not contradict the
// Omega(n log n) comparison lower bound: the algorithm does arithmetic on
// the keys, not only comparisons.
//
// The constant is bounded because the refinement depth is bounded by
// min(w, MAX_SUBDIVISION_DEPTH), and a leaf reaching a comparison sort
// with a non-zero span has size at most
//
//     B(n) = min( n, (lambda^(D+1) * 2^w / n)^(1/D) )
//
// which for large n DECREASES like n^(-1/D), and whose supremum over all
// n is the CONSTANT lambda * 2^(w/(D+1)) ~= 18000 at the default lambda,
// and at most ~5.7e6 at the ceiling lambda = 10000. The bound depends on
// n; its supremum does not, and that supremum is what makes the total
// linear.
//
// What a leaf costs depends on which local sort it reaches, and only the
// largest band is O(m log m):
//
//     m <= 64          insertion sort   O(m^2), with m <= 64
//     64 < m <= 384    quicksort        O(m^2), with m <= 384
//     m > 384          introsort        O(m log m), guaranteed by the
//                                       heapsort fallback
//
// Quicksort carries no partitioning budget of its own, so its worst case
// really is quadratic - it is bounded here only because the dispatcher
// never hands it more than 384 elements. When m <= C for a fixed C,
// m^2 <= C*m, so the cost is linear in the leaf with constant C/2 = 192.
// Leaves tile [0, n) without overlap, so sum(m) <= n and the total across
// all leaves is O(n).
//
// The guarantee holds under four hypotheses, all of them checkable:
//   H1  T is integral with w = 8*sizeof(T) <= 64 bits (static_assert'd).
//   H2  lambda >= 1, t >= lambda and D >= 1 are constants independent
//       of n. Enforced: the constructor clamps lambda into [1, 10000] and
//       t into [lambda, 10000] (Config.hpp, MAX_TARGET_ELEMENTS_PER_BIN and
//       MAX_LEAF_THRESHOLD), and D >= 1 is static_assert'd. A caller who
//       passes data.size() for either argument gets the ceiling, so the
//       largest leaf a comparison sort can receive is at most
//       10000 * 2^(w/(D+1)) ~= 5.7e6 for every configuration, and Theta(n)
//       holds for every input and every argument.
//   H3  Unit-cost RAM; allocating or releasing k words costs O(k);
//       n + lambda fits in size_t.
//   H4  memcpy of k elements costs Theta(k).
//
// 0.11.0 changed how the memory is used and nothing about the partition:
// the grids, the buckets, the refinement rule, the leaves and the local
// sorts are 0.10.0's, element for element. detail/Engine.hpp explains the
// difference; research/ALGORITHM.md needs none.
//
// ---- Guarantees ---------------------------------------------------
// STABILITY: none. This sorter is NOT stable. For the integral key types
//   it accepts, equal elements are indistinguishable, so this is
//   unobservable.
//
// THREAD SAFETY: the SETTINGS of a sorter (lambda, t) are immutable after
//   construction. Its SCRATCH lives in a Workspace:
//     - sort(data) uses the workspace the instance owns, so one instance
//       used from two threads at once is a data race, as in 0.10.0;
//     - sort(data, workspace) is const and uses only the workspace it is
//       given, so one instance may serve any number of threads at once,
//       each with its own workspace. No lock is taken; none is needed.
//   There is no shared global state.
//
// INSTRUMENTATION: STRATUM_ENABLE_METRICS is a whole-program switch, not a
//   per-file one. Defining it adds members to this class and to Workspace,
//   so they have a different size and layout in the two configurations.
//
//   That mismatch cannot corrupt a program silently. The classes live in an
//   inline namespace whose name depends on the macro (see Config.hpp), so
//   the two configurations are distinct types with distinct mangled names.
//   Callers still write stratum::StratumSort<T> and notice nothing; but if
//   two translation units disagree and pass the type across the boundary,
//   the program fails to link with an undefined symbol rather than
//   misbehaving at run time. Set the macro in the build system for every
//   target that includes this header, or leave it unset everywhere.
//
// EXCEPTIONS: strong guarantee in a release build. The only operations
//   that can throw are the allocations of the workspace, and all of them
//   happen before the first write to the caller's array. If sort() throws,
//   the input is unchanged. This does NOT hold when STRATUM_ENABLE_METRICS
//   is defined: the instrumentation records timings and leaf sizes while
//   the array is being written, and recording allocates. That build is a
//   measurement tool, not a product; tests/api_contract.cpp pins the
//   difference.
//
// MEMORY: a sort of n elements needs, in its workspace,
//
//        n * sizeof(T)                        one partner buffer
//     +  (2 * ceil(n / lambda) + 2) counters  4 bytes each when n < 2^32
//
//   and nothing else: no per-element index, no refinement tree, and no
//   term that depends on the shape of the input. Measured peak for an
//   8-byte key: 1.03x the input at the default lambda = 32 (0.10.0: 3.28x),
//   and 2.0x at lambda = t = 1 (0.10.0: 16.56x). For a 1-byte key it is
//   1.0x plus at most 2 KiB (0.10.0: 10x). An input with every key equal,
//   and an input of at most lambda elements, allocate nothing.
//   research/perf/MemoryProfile.cpp measures all of it at the allocator.
//
//   The workspace an instance owns is allocated on first use and reused
//   across later calls; it grows, never shrinks by itself, and is released
//   by releaseScratch() or when the instance is destroyed. A workspace
//   passed to sort(data, workspace) belongs to the caller.
//
// ---- Element type -------------------------------------------------
// Integral types only: every formula (span, interval width, bin index) is
// defined over exact integer arithmetic.
// ============================================================
template <typename T>
class StratumSort {
    static_assert(std::is_integral<T>::value,
                  "StratumSort requires an integral element type");
    // std::is_integral<bool> is true, but std::vector<bool> is the packed
    // specialisation: no data(), elements not individually addressable, so
    // the block copies cannot work on it. Rejecting it here gives a
    // readable message instead of a template error deep inside memcpy.
    static_assert(!std::is_same<typename std::remove_cv<T>::type, bool>::value,
                  "StratumSort does not support bool: std::vector<bool> is a packed "
                  "specialisation with no contiguous storage");
    // The span arithmetic is carried in uint64_t throughout, so a key wider
    // than 64 bits would have its offset truncated and could produce an
    // out-of-range bucket index. That is not a wrong answer, it is a heap
    // overflow: verified with __int128, which some toolchains report as
    // integral. The proof of linearity also assumes w is bounded.
    static_assert(sizeof(T) <= sizeof(uint64_t),
                  "StratumSort supports keys of at most 64 bits: the range arithmetic "
                  "is carried in uint64_t");

public:
    using Traits = IntegralKeyTraits<T>;

    // targetElementsPerBin (lambda) is the target occupancy per bin;
    // leafThreshold (t) is the size at which refinement stops. Config.hpp
    // explains what each controls and how to choose it.
    //
    // Both arguments are clamped rather than rejected, so no combination
    // can produce undefined behaviour:
    //   - lambda == 0 becomes the default (a bin must hold >= 1 element);
    //   - lambda above MAX_TARGET_ELEMENTS_PER_BIN and t above
    //     MAX_LEAF_THRESHOLD (both 10000) are lowered to it, which is what
    //     keeps the Theta(n) bound independent of the arguments;
    //   - t < lambda is raised to lambda (a bin of the target occupancy
    //     must be allowed to become a leaf).
    // The clamping is silent by design: these are tuning hints, not a
    // contract the caller can violate. Use the accessors below to see
    // what an instance actually ended up with.
    //
    // KNOWN HAZARD: both parameters are std::size_t and adjacent, so
    // swapping them at a call site compiles. StratumSort(64, 32) is
    // read as (64, 64), not (32, 64). The Parameters overload below names
    // them and removes the hazard; this one stays for compatibility.
    explicit StratumSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN,
                         std::size_t leafThreshold = DEFAULT_LEAF_THRESHOLD);

    // The same, with the two parameters named (Config.hpp, Parameters), so
    // they cannot be swapped by accident. Clamped identically.
    explicit StratumSort(const Parameters& parameters)
        : StratumSort(parameters.targetElementsPerBin, parameters.leafThreshold) {}

    // Sorts 'data' in place into ascending order, using the workspace this
    // instance owns. Not safe to call concurrently on one instance.
    void sort(std::vector<T>& data);

    // Same, for any contiguous range [first, last).
    void sort(T* first, T* last);

    // Sorts using the caller's workspace instead of the instance's own.
    // Const: many threads may call it on ONE sorter at the same time,
    // provided each passes its own workspace.
    void sort(std::vector<T>& data, Workspace<T>& workspace) const;
    void sort(T* first, T* last, Workspace<T>& workspace) const;

    std::size_t targetElementsPerBin() const { return targetElementsPerBin_; }
    std::size_t leafThreshold() const { return leafThreshold_; }

    // Bytes of scratch this instance currently holds, and a way to give
    // them back without destroying the instance.
    std::size_t scratchBytes() const { return workspace_.bytes(); }
    void releaseScratch() noexcept { workspace_.release(); }

#ifdef STRATUM_ENABLE_METRICS
    // Statistics from the last sort() call that used the instance's own
    // workspace. Research build only.
    const SortMetrics& metrics() const { return workspace_.metrics(); }

    // Lets analysis code inspect the partition Stratum Sort would produce,
    // without sorting its leaves. 'inBufferA' means the leaf is in the
    // array being sorted (debugBufferA), otherwise in the workspace
    // (debugBufferB); both are indexed by the same absolute offsets.
    struct LeafView {
        bool inBufferA;
        std::size_t start;
        std::size_t count;
    };

    std::vector<LeafView> debugPartitionOnly(const std::vector<T>& data);

    const std::vector<T>& debugBufferA() const { return debugA_; }
    const std::vector<T>& debugBufferB() const { return debugB_; }
#endif

private:
    void sortRange(T* first, std::size_t n, Workspace<T>& workspace) const;

    std::size_t targetElementsPerBin_;   // lambda: target occupancy
    std::size_t leafThreshold_;          // t: base-case size
    Workspace<T> workspace_;

#ifdef STRATUM_ENABLE_METRICS
    std::vector<T> debugA_;
    std::vector<T> debugB_;
#endif
};

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum

#include "StratumSort.tpp"
#include "Sort.hpp"
