#pragma once

// Included by StratumSort.hpp; not meant to be included directly.

// ============================================================
// The Stratum Sort engine
// ============================================================
// One engine sorts every element type. It sees elements only through a
// traits class (KeyTraits.hpp): key() maps an element to a uint64_t that
// preserves the order, less() is that order. For the integral keys of
// 0.10.0 the key is the value itself, sign-flipped when signed, and every
// span, width and bucket index is the number 0.10.0 computed.
//
// WHAT CHANGED FROM 0.10.0, AND WHAT DID NOT
//
// The partition is identical. planTop() and planSplit() are the formulas of
// 0.10.0's planPartition() and planRefinement(), bucket for bucket; the
// distribution is still a stable count-then-place; refinement still uses
// the bin's own observed range, stops at count <= t, depth >= D or span 0;
// leaves still go to the same local sorts at the same thresholds. Each leaf
// even receives its elements in the same order, because every distribution
// step is stable and starts from the same order. research/ALGORITHM.md's
// proof is a statement about exactly these objects and needs no change.
//
// How the memory is used is what changed. 0.10.0 allocated, for n elements
// of an 8-byte key at lambda = 32, 26.25 bytes per element:
//
//     2n * sizeof(T)          two cascading buffers           16    B/elem
//     n  * sizeof(size_t)     a cached bucket index            8    B/elem
//     O(n / lambda) nodes     the refinement tree, 48 B/bin   ~1.5  B/elem
//     O(n / lambda)           histograms and cursors          ~0.75 B/elem
//
// Every row but the last was a consequence of the implementation, not of
// the algorithm, and each one goes:
//
//   * THE CALLER'S ARRAY IS ONE OF THE TWO BUFFERS. The top-level split
//     copies the input into the workspace while it counts, then places it
//     back into the caller's array; from there the levels alternate between
//     the two, at the same absolute offsets, as before. Top-level bins -
//     the leaves of every non-adversarial input - end where they belong and
//     are never copied again. A leaf at odd depth is copied back once.
//
//   * NO CACHED BUCKET INDEX. The count pass and the place pass both compute
//     floor(offset / width), with an exact reciprocal (FastDivision.hpp)
//     instead of a divide instruction. Measured on the distribution step
//     alone, recomputing is 23-52% FASTER than 0.10.0's store-and-reload
//     (research/perf/BucketIndexStrategies.cpp) - the cache was costing
//     time as well as memory.
//
//   * NO TREE. 0.10.0 built the whole refinement tree, then sorted its
//     leaves, then joined them, so every node had to exist at once. Here a
//     node is refined, its children are visited depth-first, and each leaf
//     is sorted and put in place the moment it is found. What survives
//     between levels is one histogram per level on the current path.
//
//   * THE HISTOGRAMS SHARE ONE ARENA of 2 * ceil(n / lambda) + 2 counters,
//     32-bit when n < 2^32. A split takes its s counters from the top of
//     the arena and, once its elements are placed, those counters hold the
//     end of every child, which is how the children are found. If keeping
//     them could starve a descendant of counters, the split gives them back
//     at once and finds its children by galloping search instead: the
//     buckets of a placed node are non-decreasing along it, so the end of
//     each child is an exponential-then-binary search away, O(log size)
//     bucket computations per child. Why 2 * ceil(n / lambda) always
//     suffices is proved next to visitChildren().
//
// Peak auxiliary memory is therefore n * sizeof(T) + (2 ceil(n/lambda)+2)
// counters - 8.25 bytes per element for an 8-byte key at lambda = 32,
// against 26.25 - and nothing in it depends on the input's shape.
//
// The last term is structural. A split into ceil(m / lambda) buckets needs
// one counter per bucket: that fan-out IS the algorithm (it is what Lemma 4
// spends the bit budget on), so O(n / lambda) counters cannot go without
// changing the algorithm. The n-element buffer can: the in-place variant
// is discussed in research/history/V11_memory.md, and why it is not the
// default.
//
// EXCEPTION SAFETY IS UNCHANGED. Every allocation - the buffer and the
// arena, both sized from n before anything is read twice - happens before
// the first write to the caller's array. After that the engine allocates
// nothing and calls nothing that can throw (the element type is trivially
// copyable, the key and the order are plain functions), so the strong
// guarantee of 0.10.0 still holds, in the release build.
// ============================================================

#include "../Config.hpp"
#include "../KeyTraits.hpp"
#include "../Workspace.hpp"
#include "FastDivision.hpp"

#ifdef STRATUM_ENABLE_METRICS
#include "../Metrics.hpp"
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {
namespace detail {

// ============================================================
// Instrumentation
// ============================================================
// The only place STRATUM_ENABLE_METRICS appears in the engine. In a release
// build every member is an empty inline function and the probe is an
// empty object; in the research build it forwards to a SortMetrics.
class Probe {
public:
#ifdef STRATUM_ENABLE_METRICS
    explicit Probe(SortMetrics* m) : m_(m) {}
    void comparisons(std::size_t k) const { m_->recordComparisons(k); }
    void localAlgorithm(const char* name) const { m_->recordAlgorithmUsage(name); }
    void leaf(std::size_t count, bool empty) const { m_->recordBin(count, empty); }
    void split(std::size_t size, std::size_t children, std::size_t largest, std::size_t depth) const {
        m_->recordSubdivision(depth);
        m_->recordSubdivisionQuality(size, children, largest, depth);
    }
    void memory(std::size_t bytes) const { m_->addApproxMemory(bytes); }
    void begin(const char* phase) const { m_->startPhase(phase); }
    void end(const char* phase) const { m_->endPhase(phase); }

private:
    SortMetrics* m_;
#else
    Probe() = default;
    void comparisons(std::size_t) const {}
    void localAlgorithm(const char*) const {}
    void leaf(std::size_t, bool) const {}
    void split(std::size_t, std::size_t, std::size_t, std::size_t) const {}
    void memory(std::size_t) const {}
    void begin(const char*) const {}
    void end(const char*) const {}
#endif
};

// floor(log2(v)) for v >= 1, by integer arithmetic. Deliberately not
// std::log2: this value controls control flow, and floating point would
// invite platform-dependent rounding.
inline std::size_t floorLog2(std::size_t v) {
    std::size_t r = 0;
    while (v > 1) {
        v >>= 1;
        ++r;
    }
    return r;
}

// ============================================================
// Local sorts
// ============================================================
// The comparison sorts that finish a leaf, and the run detection in front
// of them. Unchanged from 0.10.0 except that they work on a pointer range
// and compare through Traits::less, so they sort any element type; for an
// integral type less(a, b) is a < b and the code is the code 0.10.0 ran,
// comparison for comparison.
template <typename Traits>
struct LocalSort {
    using E = typename Traits::Element;
    using Index = std::ptrdiff_t;

    static bool less(const E& a, const E& b) { return Traits::less(a, b); }

    enum class RunShape { Ascending, Descending, Unsorted };

    // One O(k) scan recognising a range that is already ascending or
    // descending. NOT STABLE: reversing a descending run swaps elements
    // with equal keys. Unobservable for integral keys; the stable variant
    // uses stableRun() instead.
    static RunShape detectRun(const E* buf, std::size_t count, const Probe& probe) {
        if (count < 2) return RunShape::Ascending;
        bool ascending = true;
        bool descending = true;
        for (std::size_t i = 1; i < count; ++i) {
            probe.comparisons(2);
            if (less(buf[i], buf[i - 1])) ascending = false;
            if (less(buf[i - 1], buf[i])) descending = false;
            if (!ascending && !descending) return RunShape::Unsorted;
        }
        return ascending ? RunShape::Ascending : RunShape::Descending;
    }

    static void insertionSort(E* arr, Index left, Index right, const Probe& probe) {
        for (Index i = left + 1; i <= right; ++i) {
            const E key = arr[i];
            Index j = i - 1;
            while (j >= left) {
                probe.comparisons(1);
                if (!less(key, arr[j])) break; // arr[j] <= key: stable
                arr[j + 1] = arr[j];
                --j;
            }
            arr[j + 1] = key;
        }
    }

    // Median-of-three Hoare partition. The median-of-three step leaves
    // arr[left] <= pivot <= arr[right] and parks the pivot at right-1; those
    // two act as sentinels, which is why neither scan tests its bound.
    static Index partition(E* arr, Index left, Index right, const Probe& probe) {
        assert(right - left >= 2 && "partition needs at least three elements for the sentinels");
        const Index mid = left + (right - left) / 2;
        probe.comparisons(3);
        if (less(arr[mid], arr[left])) std::swap(arr[mid], arr[left]);
        if (less(arr[right], arr[left])) std::swap(arr[right], arr[left]);
        if (less(arr[right], arr[mid])) std::swap(arr[right], arr[mid]);

        const E pivot = arr[mid];
        std::swap(arr[mid], arr[right - 1]);

        Index i = left;
        Index j = right - 1;
        while (true) {
            do {
                ++i;
                probe.comparisons(1);
            } while (less(arr[i], pivot));
            do {
                --j;
                probe.comparisons(1);
            } while (less(pivot, arr[j]));
            if (i >= j) break;
            std::swap(arr[i], arr[j]);
        }
        std::swap(arr[i], arr[right - 1]);
        return i;
    }

    // Recurses into the smaller side, loops on the larger: O(log k) stack.
    // No depth budget, so its worst case is quadratic; it only ever sees
    // leaves of at most LOCAL_QUICKSORT_MAX_ELEMENTS, which is what keeps
    // that bounded (research/ALGORITHM.md, section 8.6).
    static void quickSort(E* arr, Index left, Index right, const Probe& probe) {
        while (right - left > static_cast<Index>(LOCAL_PARTITION_CUTOFF)) {
            const Index p = partition(arr, left, right, probe);
            if (p - left < right - p) {
                quickSort(arr, left, p - 1, probe);
                left = p + 1;
            } else {
                quickSort(arr, p + 1, right, probe);
                right = p - 1;
            }
        }
        insertionSort(arr, left, right, probe);
    }

    static void introSort(E* arr, Index left, Index right, const Probe& probe) {
        if (right <= left) return;
        const std::size_t n = static_cast<std::size_t>(right - left + 1);
        introSortImpl(arr, left, right, INTROSORT_DEPTH_FACTOR * floorLog2(n), probe);
    }

    static void introSortImpl(E* arr, Index left, Index right, std::size_t depthLimit,
                              const Probe& probe) {
        while (right - left > static_cast<Index>(LOCAL_PARTITION_CUTOFF)) {
            if (depthLimit == 0) {
                heapSort(arr, left, right, probe);
                return;
            }
            --depthLimit;
            const Index p = partition(arr, left, right, probe);
            if (p - left < right - p) {
                introSortImpl(arr, left, p - 1, depthLimit, probe);
                left = p + 1;
            } else {
                introSortImpl(arr, p + 1, right, depthLimit, probe);
                right = p - 1;
            }
        }
        insertionSort(arr, left, right, probe);
    }

    // Max-heap repair at 'root' within the heap [base, end]. The children
    // of heap node k are 2k+1 and 2k+2 counted from 'base' - the heap's
    // first slot - not from the node: folding the two together is the
    // defect that shipped in 0.9.0 (see the 0.10.0 changelog).
    static void siftDown(E* arr, Index base, Index root, Index end, const Probe& probe) {
        const Index last = end - base;
        if (last < 1) return;
        while (root - base <= (last - 1) / 2) {
            const Index child = base + 2 * (root - base) + 1;
            Index swapIdx = root;
            probe.comparisons(1);
            if (less(arr[swapIdx], arr[child])) swapIdx = child;
            if (child + 1 <= end) {
                probe.comparisons(1);
                if (less(arr[swapIdx], arr[child + 1])) swapIdx = child + 1;
            }
            if (swapIdx == root) return;
            std::swap(arr[root], arr[swapIdx]);
            root = swapIdx;
        }
    }

    static void heapSort(E* arr, Index left, Index right, const Probe& probe) {
        const Index n = right - left + 1;
        if (n < 2) return;
        probe.localAlgorithm("HeapSort");
        for (Index start = left + (n - 2) / 2; start >= left; --start)
            siftDown(arr, left, start, right, probe);
        for (Index end = right; end > left; --end) {
            std::swap(arr[left], arr[end]);
            siftDown(arr, left, left, end - 1, probe);
        }
    }

    // The unstable leaf sort: 0.10.0's sortLeaf. The dispatch thresholds
    // describe the local sorts and nothing else - they are independent of
    // the leaf threshold t, on purpose.
    static void sortLeaf(E* buf, std::size_t count, const Probe& probe) {
        if (count < 2) return;
        switch (detectRun(buf, count, probe)) {
            case RunShape::Ascending:
                probe.localAlgorithm("AlreadySorted");
                return;
            case RunShape::Descending:
                std::reverse(buf, buf + count);
                probe.localAlgorithm("ReversedRun");
                return;
            case RunShape::Unsorted:
                break;
        }
        const Index right = static_cast<Index>(count - 1);
        if (count <= LOCAL_INSERTION_MAX_ELEMENTS) {
            insertionSort(buf, 0, right, probe);
            probe.localAlgorithm("InsertionSort");
        } else if (count <= LOCAL_QUICKSORT_MAX_ELEMENTS) {
            quickSort(buf, 0, right, probe);
            probe.localAlgorithm("QuickSort");
        } else {
            introSort(buf, 0, right, probe);
            probe.localAlgorithm("Introsort");
        }
    }
};

// ============================================================
// Interval formulas
// ============================================================
// Exactly 0.10.0's. A range is described by its SPAN, max - min, never by
// max - min + 1, which needs w + 1 bits and wraps to zero for a key that
// spans the whole universe. research/ALGORITHM.md section 5 proves:
//   PROPERTY 1  width = span / s + 1 equals ceil((span + 1) / s);
//   PROPERTY 2  floor(offset / width) <= s - 1 for every offset in [0, span],
//               so the bucket index needs no clamp.
struct Grid {
    uint64_t origin = 0;       // key mapped to bucket 0
    uint64_t width = 1;        // keys per bucket
    std::size_t binCount = 1;  // number of buckets
};

inline std::size_t ceilDiv(std::size_t a, std::size_t b) { return a / b + (a % b != 0); }

// The top-level grid (0.10.0's planPartition). Can return a single bucket:
// for n <= lambda, and for span == 0 (the caller handles that one first).
inline Grid planTop(uint64_t minKey, uint64_t maxKey, std::size_t length, std::size_t lambda) {
    assert(length > 0);
    const uint64_t span = maxKey - minKey;
    Grid g;
    g.origin = minKey;
    g.binCount = ceilDiv(length, lambda);
    if (g.binCount == 0) g.binCount = 1;
    // The cap: a bucket narrower than one value is unreachable. It cannot
    // change the partition - when it binds the width is 1 before and after.
    if (span < static_cast<uint64_t>(g.binCount)) g.binCount = static_cast<std::size_t>(span + 1);
    // With one bucket the width is meaningless and span + 1 may not even
    // be representable; normalising to 1 keeps "width >= 1" unconditional.
    g.width = (g.binCount == 1) ? 1 : span / static_cast<uint64_t>(g.binCount) + 1;
    assert(g.binCount >= 1 && g.width >= 1);
    return g;
}

// A refinement grid (0.10.0's planRefinement): same rule over the bin's OWN
// observed range. Precondition: minKey < maxKey and count > t >= lambda.
inline Grid planSplit(uint64_t minKey, uint64_t maxKey, std::size_t count, std::size_t lambda) {
    assert(minKey < maxKey);
    const uint64_t span = maxKey - minKey;
    Grid g;
    g.origin = minKey;
    g.binCount = ceilDiv(count, lambda);
    if (span < static_cast<uint64_t>(g.binCount)) g.binCount = static_cast<std::size_t>(span + 1);
    // Always at least two buckets: count > t >= lambda gives >= 2, and a
    // binding cap gives span + 1 >= 2. That is what guarantees progress.
    assert(g.binCount >= 2);
    g.width = span / static_cast<uint64_t>(g.binCount) + 1;
    return g;
}

// ============================================================
// Leaf sinks
// ============================================================
// What happens when refinement stops. SortLeaves is the algorithm;
// RecordLeaves is the research build's introspection, which records where
// each leaf is instead of sorting it.
struct SortLeaves {
    static constexpr bool kRecord = false;
    void record(bool, std::size_t, std::size_t) {}
};

#ifdef STRATUM_ENABLE_METRICS
struct LeafRecord {
    bool inData;
    std::size_t start;
    std::size_t count;
};
struct RecordLeaves {
    static constexpr bool kRecord = true;
    std::vector<LeafRecord>* out;
    void record(bool inData, std::size_t start, std::size_t count) {
        if (count > 0) out->push_back({inData, start, count});
    }
};
#endif

// ============================================================
// Engine
// ============================================================
template <typename Traits, typename Count, typename Sink>
class Engine {
public:
    using E = typename Traits::Element;

    // Counters the arena needs for an input whose top-level split has
    // topBins buckets: see visitChildren().
    static std::size_t arenaFor(std::size_t topBins) { return 2 * topBins + 2; }

    Engine(E* data, E* aux, Count* arena, std::size_t arenaCapacity, std::size_t lambda,
           std::size_t leafThreshold, const Probe& probe, Sink& sink)
        : data_(data),
          aux_(aux),
          arena_(arena),
          arenaCapacity_(arenaCapacity),
          lambda_(lambda),
          leafThreshold_(leafThreshold),
          probe_(probe),
          sink_(sink) {}

    // Sorts data_[0, n). 'top' is the top-level grid and has at least two
    // buckets; the caller has handled n <= lambda and span == 0.
    void run(std::size_t n, const Grid& top) {
        assert(top.binCount >= 2);
        assert(arenaCapacity_ >= arenaFor(top.binCount));
        static_assert(sizeof(Count) >= sizeof(uint32_t), "counters must hold any position");

        probe_.begin("distribute");
        const FastDivider64 divider(top.width);
        Count* ends = arena_;
        const std::size_t s = top.binCount;
        std::fill(ends, ends + s, Count{0});

        // Count, copying the input into the workspace on the way: this pass
        // reads every element anyway, and the copy is what frees the
        // caller's array to receive the placed elements.
        const uint64_t origin = top.origin;
        E* const in = data_;
        E* const out = aux_;
        divider.dispatch([&](const auto& bucketOf) {
            for (std::size_t i = 0; i < n; ++i) {
                const E e = in[i];
                out[i] = e;
                const uint64_t b = bucketOf(Traits::key(e) - origin);
                assert(b < s); // Property 2
                ++ends[b];
            }
        });
        toCursors(ends, s, 0);
        divider.dispatch([&](const auto& bucketOf) {
            for (std::size_t i = 0; i < n; ++i) {
                const E e = out[i];
                in[ends[bucketOf(Traits::key(e) - origin)]++] = e;
            }
        });
        probe_.end("distribute");
        probe_.memory(n * sizeof(E) + arenaCapacity_ * sizeof(Count));

        // The root keeps its ends: the arena holds 2s, and no descendant
        // can ever need more than s (see visitChildren()).
        probe_.begin("refine");
        arenaTop_ = s;
        visitChildren(/*inData=*/true, 0, n, top, divider, ends, /*childDepth=*/0);
        arenaTop_ = 0;
        probe_.end("refine");
    }

    // A whole array that is one leaf (n <= lambda): sorted where it is.
    void runSingleLeaf(std::size_t n) {
        probe_.begin("refine");
        leaf(/*inData=*/true, 0, n);
        probe_.end("refine");
    }

private:
    E* buffer(bool inData) const { return inData ? data_ : aux_; }

    // Histogram -> write cursors, in place. After the place pass each
    // cursor has advanced to the END of its bucket, which is what
    // visitChildren() reads.
    static void toCursors(Count* c, std::size_t s, std::size_t base) {
        std::size_t cursor = base;
        for (std::size_t b = 0; b < s; ++b) {
            const std::size_t k = c[b];
            c[b] = static_cast<Count>(cursor);
            cursor += k;
        }
    }

    // ---- Refinement --------------------------------------------------
    // 0.10.0's refine(), minus the tree: a bin at [start, start + count) of
    // whichever buffer holds it becomes a leaf when it is empty, holds at
    // most t elements, has reached depth D, or has span 0; otherwise it is
    // split with its OWN observed range into the other buffer, at the same
    // offsets, and its children are visited at depth + 1.
    void process(bool inData, std::size_t start, std::size_t count, std::size_t depth) {
        if (count == 0) {
            probe_.leaf(0, true);
            return;
        }
        // Base case by size or exhausted depth, BEFORE the range scan: a bin
        // that stops here never pays for a range it does not need.
        if (count <= leafThreshold_ || depth >= MAX_SUBDIVISION_DEPTH) {
            probe_.leaf(count, false);
            leaf(inData, start, count);
            return;
        }

        const E* const src = buffer(inData) + start;
        uint64_t lo = Traits::key(src[0]);
        uint64_t hi = lo;
        for (std::size_t i = 1; i < count; ++i) {
            const uint64_t k = Traits::key(src[i]);
            if (k < lo) lo = k;
            if (k > hi) hi = k;
        }
        if (lo == hi) {
            // Span 0: every key is equal, the bin is sorted by definition.
            probe_.leaf(count, false);
            certifiedLeaf(inData, start, count);
            return;
        }

        const Grid g = planSplit(lo, hi, count, lambda_);
        const std::size_t s = g.binCount;
        assert(arenaTop_ + s <= arenaCapacity_);
        Count* const ends = arena_ + arenaTop_;
        std::fill(ends, ends + s, Count{0});

        E* const dst = buffer(!inData);
        const uint64_t origin = g.origin;
        const FastDivider64 divider(g.width);
        divider.dispatch([&](const auto& bucketOf) {
            for (std::size_t i = 0; i < count; ++i) {
                const uint64_t b = bucketOf(Traits::key(src[i]) - origin);
                assert(b < s); // Property 2
                ++ends[b];
            }
        });
        toCursors(ends, s, start);
        divider.dispatch([&](const auto& bucketOf) {
            for (std::size_t i = 0; i < count; ++i) {
                const E e = src[i];
                dst[ends[bucketOf(Traits::key(e) - origin)]++] = e;
            }
        });

        std::size_t largest = 0;
        std::size_t previous = start;
        for (std::size_t b = 0; b < s; ++b) {
            largest = std::max<std::size_t>(largest, static_cast<std::size_t>(ends[b]) - previous);
            previous = ends[b];
        }
        assert(previous == start + count);
        probe_.split(count, s, largest, depth + 1);

        // Keep the ends only if what is left still covers the most any
        // descendant can ask for (see visitChildren()).
        const bool keep = arenaCapacity_ - arenaTop_ - s >= ceilDiv(largest, lambda_);
        if (keep) {
            arenaTop_ += s;
            visitChildren(!inData, start, start + count, g, divider, ends, depth + 1);
            arenaTop_ -= s;
        } else {
            visitChildren(!inData, start, start + count, g, divider, nullptr, depth + 1);
        }
    }

    // Visits the children of a node that has just been placed into
    // [begin, end) of one buffer, in value order.
    //
    // WITH 'ends' the child boundaries are read from the histogram.
    // WITHOUT, they are found by galloping: along a placed node the bucket
    // index is non-decreasing (Property 4), so the end of the child that
    // starts at 'pos' is the first position whose bucket differs, found by
    // an exponential probe then a binary search - O(log size) bucket
    // computations per non-empty child, O(size) per node at worst, so the
    // per-level Theta(n) of the proof is untouched.
    //
    // WHY 2 * topBins COUNTERS ALWAYS SUFFICE. Invariant: when process() is
    // called on a node of m elements, at least ceil(m / lambda) counters are
    // free. A split of that node needs s <= ceil(m / lambda) (planSplit),
    // so it always finds them. It then keeps its s ends only if at least
    // ceil(largest child / lambda) remain free afterwards - which restores
    // the invariant for every child - and otherwise releases them, leaving
    // the node's own ceil(m / lambda) >= ceil(child / lambda) free. For the
    // top-level bins, 2 * topBins - topBins = topBins >= ceil(n / lambda) >=
    // ceil(m / lambda) whenever the top-level cap does not bind; when it
    // binds, every top-level bin has span 0 (Lemma 2) and none splits.
    // Induction over the depth does the rest. The +2 is slack for the
    // assertions, not for the argument.
    void visitChildren(bool inData, std::size_t begin, std::size_t end, const Grid& g,
                       const FastDivider64& divider, const Count* ends, std::size_t childDepth) {
        if (ends != nullptr) {
            std::size_t previous = begin;
            for (std::size_t b = 0; b < g.binCount; ++b) {
                const std::size_t next = ends[b];
                process(inData, previous, next - previous, childDepth);
                previous = next;
            }
            return;
        }
        const E* const buf = buffer(inData);
        const uint64_t origin = g.origin;
        std::size_t visited = 0;
        std::size_t pos = begin;
        while (pos < end) {
            std::size_t next = end;
            divider.dispatch([&](const auto& bucketOf) {
                next = runEnd(buf, pos, end, origin, bucketOf);
            });
            process(inData, pos, next - pos, childDepth);
            pos = next;
            ++visited;
        }
        // Empty buckets are invisible to the search; tell the research
        // build how many there were, so its bin counts do not depend on
        // which of the two paths ran.
        for (std::size_t k = visited; k < g.binCount; ++k) probe_.leaf(0, true);
    }

    // End of the run of equal buckets starting at 'pos', in [pos + 1, end].
    template <typename BucketOf>
    static std::size_t runEnd(const E* buf, std::size_t pos, std::size_t end, uint64_t origin,
                              const BucketOf& bucketOf) {
        const uint64_t b = bucketOf(Traits::key(buf[pos]) - origin);
        std::size_t lo = pos; // bucket(buf[lo]) == b
        std::size_t hi = end; // exclusive; bucket(buf[hi]) != b if hi < end
        std::size_t step = 1;
        while (end - lo > step) {
            const std::size_t probe = lo + step;
            if (bucketOf(Traits::key(buf[probe]) - origin) != b) {
                hi = probe;
                break;
            }
            lo = probe;
            step *= 2;
        }
        while (hi - lo > 1) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (bucketOf(Traits::key(buf[mid]) - origin) == b)
                lo = mid;
            else
                hi = mid;
        }
        return hi;
    }

    // ---- Leaves ------------------------------------------------------
    // Sorted where they are; a leaf in the workspace is then copied to its
    // final position, which is its own offset (Property 4: leaves tile
    // [0, n) in value order).
    void leaf(bool inData, std::size_t start, std::size_t count) {
        if constexpr (Sink::kRecord) {
            sink_.record(inData, start, count);
            return;
        }
        probe_.begin("localSort");
        LocalSort<Traits>::sortLeaf(buffer(inData) + start, count, probe_);
        probe_.end("localSort");
        if (!inData) copyHome(start, count);
    }

    void certifiedLeaf(bool inData, std::size_t start, std::size_t count) {
        if constexpr (Sink::kRecord) {
            sink_.record(inData, start, count);
            return;
        }
        probe_.localAlgorithm("AlreadySorted");
        if (!inData) copyHome(start, count);
    }

    void copyHome(std::size_t start, std::size_t count) {
        probe_.begin("join");
        std::memcpy(static_cast<void*>(data_ + start), static_cast<const void*>(aux_ + start),
                    count * sizeof(E));
        probe_.end("join");
    }

    E* const data_;
    E* const aux_;
    Count* const arena_;
    const std::size_t arenaCapacity_;
    std::size_t arenaTop_ = 0;
    const std::size_t lambda_;
    const std::size_t leafThreshold_;
    const Probe probe_;
    Sink& sink_;
};

// ============================================================
// Driver
// ============================================================
struct KeyRange {
    uint64_t minKey;
    uint64_t maxKey;
};

// Phase 1: one pass for the minimum and maximum key. Every later formula is
// relative to the observed range, so this pass must complete first.
template <typename Traits>
KeyRange analyze(const typename Traits::Element* data, std::size_t n) {
    assert(n > 0);
    uint64_t lo = Traits::key(data[0]);
    uint64_t hi = lo;
    for (std::size_t i = 1; i < n; ++i) {
        const uint64_t k = Traits::key(data[i]);
        if (k < lo) lo = k;
        if (k > hi) hi = k;
    }
    return {lo, hi};
}

// Sorts data[0, n) with the given (already clamped) parameters, using
// 'workspace' for every byte of scratch.
template <typename Traits, typename Sink>
void sortWith(typename Traits::Element* data, std::size_t n, std::size_t lambda,
              std::size_t leafThreshold, Workspace<typename Traits::Element>& workspace,
              const Probe& probe, Sink& sink) {
    using E = typename Traits::Element;
    if (n < 2) {
        if (Sink::kRecord && n == 1) sink.record(true, 0, 1);
        return;
    }

    probe.begin("analyze");
    const KeyRange range = analyze<Traits>(data, n);
    probe.end("analyze");

    if (range.minKey == range.maxKey) {
        // Every key equal: sorted as it stands (0.10.0 reached the same
        // conclusion one scan later, through the span-0 certificate).
        probe.leaf(n, false);
        probe.localAlgorithm("AlreadySorted");
        if (Sink::kRecord) sink.record(true, 0, n);
        return;
    }

    const Grid top = planTop(range.minKey, range.maxKey, n, lambda);

    if (top.binCount == 1) {
        // n <= lambda (Case A1 of Lemma 4): one leaf, sorted where it is.
        // Nothing is split, so nothing is allocated.
        Engine<Traits, uint32_t, Sink> engine(data, nullptr, nullptr, 0, lambda, leafThreshold,
                                              probe, sink);
        engine.runSingleLeaf(n);
        return;
    }

    // EVERY allocation of the sort happens here, before the first write.
    E* const aux = WorkspaceAccess::elements(workspace, n);
    if (n <= std::numeric_limits<uint32_t>::max()) {
        using Count = uint32_t;
        const std::size_t cap = Engine<Traits, Count, Sink>::arenaFor(top.binCount);
        Count* const arena = WorkspaceAccess::counts<E, Count>(workspace, cap);
        Engine<Traits, Count, Sink>(data, aux, arena, cap, lambda, leafThreshold, probe, sink)
            .run(n, top);
    } else {
        using Count = uint64_t;
        const std::size_t cap = Engine<Traits, Count, Sink>::arenaFor(top.binCount);
        Count* const arena = WorkspaceAccess::counts<E, Count>(workspace, cap);
        Engine<Traits, Count, Sink>(data, aux, arena, cap, lambda, leafThreshold, probe, sink)
            .run(n, top);
    }
}

} // namespace detail
} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
