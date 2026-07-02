#pragma once

#include <cstddef>

// ============================================================
// Global configuration for Dynamic Range Sort (DRS)
// ============================================================
//
// DEBUG_METRICS is the single switch that turns the internal
// metrics system (DRSMetrics) on or off for the whole project.
//
//   1 -> every phase, comparison, subdivision and bin statistic
//        is recorded by DRSMetrics.
//   0 -> every DRSMetrics recording method compiles down to an
//        empty inline function body, so there is effectively no
//        runtime cost left in the sort path.
//
// Nothing else in the codebase needs to change to enable/disable
// metrics collection.
// ============================================================
#define DEBUG_METRICS 1

namespace drs {

// Default number of elements targeted per bin (see FORMULAS section
// of the algorithm specification: targetElementsPerBin).
constexpr std::size_t DEFAULT_TARGET_ELEMENTS_PER_BIN = 16;

// Bins with <= this many elements are locally sorted with Insertion Sort.
constexpr std::size_t INSERTION_SORT_THRESHOLD = 16;

// Bins with <= this many elements (and > INSERTION_SORT_THRESHOLD) are
// locally sorted with QuickSort. Bins larger than this use Introsort.
constexpr std::size_t QUICKSORT_THRESHOLD = 100;

// Maximum number of times a single bin may be recursively refined
// (re-subdivided based on its own observed range) before the remainder
// is handed off to local sorting regardless of its size. This bounds
// the cost of refinement to O(depth) extra linear passes over the
// affected elements and guarantees the algorithm always terminates,
// even for adversarial or pathological distributions.
constexpr std::size_t MAX_SUBDIVISION_DEPTH = 6;

} // namespace drs
