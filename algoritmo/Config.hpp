#pragma once

#include <cstddef>

// ============================================================
// Global configuration for Dynamic Range Sort (DRS) - v7
// ============================================================
//
// DRS_ENABLE_METRICS is the single macro that separates the production
// build from the research build:
//
//   Undefined (production): DRSMetrics, the debug/introspection API
//   (debugPartitionOnly), and every instrumentation call site are
//   compiled out entirely - not disabled at runtime, not present in the
//   compiled object code at all. The production binary contains no
//   histograms, timers, counters, correlations, or logging of any kind.
//
//   Defined (research, e.g. `-DDRS_ENABLE_METRICS`): the full metrics
//   system from v4-v6 is compiled in, unchanged in behavior.
//
// v4-v6 used a runtime constant (DEBUG_METRICS) for this instead; v7
// replaces that with a compile-time macro because runtime-disabled
// metrics code still occupies the binary and can still show up in
// profiles and instruction-cache pressure even when every call is a
// no-op. See ANALYSIS_v7.md, "2. Separar produccion e investigacion",
// for the measured difference between the two builds.
namespace drs {

// Default number of elements targeted per bin. Retuned experimentally in
// v3 (16 -> 64); no data since then has justified a different value (see
// ANALYSIS_v5.md "Investigacion 2a" and ANALYSIS.md's target sweep).
constexpr std::size_t DEFAULT_TARGET_ELEMENTS_PER_BIN = 64;

// Bins with <= this many elements are locally sorted with Insertion Sort.
constexpr std::size_t INSERTION_SORT_THRESHOLD = DEFAULT_TARGET_ELEMENTS_PER_BIN;

// Bins with <= this many elements (and > INSERTION_SORT_THRESHOLD) are
// locally sorted with QuickSort. Bins larger than this use Introsort.
constexpr std::size_t QUICKSORT_THRESHOLD = DEFAULT_TARGET_ELEMENTS_PER_BIN * 6;

// Maximum number of times a single bin may be recursively refined before
// the remainder is handed to local sorting regardless of its size. Bounds
// refinement cost to O(depth) extra linear passes and guarantees
// termination even for adversarial distributions.
constexpr std::size_t MAX_SUBDIVISION_DEPTH = 6;

} // namespace drs
