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
// the remainder is handed to local sorting regardless of its size.
//
// This is NOT a safety net that costs asymptotic quality - it is what
// makes the worst case linear, which is the opposite of what v9 initially
// assumed. Each refinement level consumes log2(splits) bits of the bin's
// observed span, and a span has at most w bits, so a bin can only survive
// D degenerate levels if D * log2(m/target) <= w, i.e.
//
//     m  <=  target * 2^(w/D)  =  64 * 2^(64/6)  ~=  104032
//
// The residual handed to Introsort is therefore bounded by a CONSTANT
// independent of n, and its aggregate cost is n*log2(104032) ~= 17n.
// Measured confirmation: the comparison-count exponent of the adversarial
// dataset is 0.994 [0.993, 0.995] over three orders of magnitude of n.
// See documentacion/COMPLEXITY_REVIEW_v9.md and
// documentacion/O8_resolucion_y_reversion_paso3.md.
//
// HARD CONSTRAINT: raising DEFAULT_TARGET_ELEMENTS_PER_BIN to 1024 or
// beyond makes m_max ~= 1.66e6, which is no longer small compared to
// realistic n, and the Theta(n log n) term reappears. Any change to the
// target must re-check this bound.
constexpr std::size_t MAX_SUBDIVISION_DEPTH = 6;

} // namespace drs
