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
// This is the ONLY local sorting path: refinement is terminal, so no bin
// larger than this can ever reach the local sort (see below).
constexpr std::size_t INSERTION_SORT_THRESHOLD = DEFAULT_TARGET_ELEMENTS_PER_BIN;

// Refinement is terminal: there is no depth cap and no fallback to a
// general comparison sort. A bin stops being refined only when it holds
// at most INSERTION_SORT_THRESHOLD elements, or when its observed range
// has collapsed to a single value (in which case it is already sorted).
//
// This terminates, and the depth is bounded by the width of the key type:
// while a bin is refined its observed span strictly decreases (the width
// is ceil((span+1)/s) with s >= 2), and every level consumes at least one
// bit of the span, so no root-to-leaf path can be longer than the number
// of bits in the span. See documentacion/RESEARCH_refinamiento_terminal.md
// (Lemas 1, 3, 3' y Teorema 4) and STEP3_terminal_refinement.md for the
// measured confirmation.
//
// The constant below is therefore NOT a policy: it is the bound that the
// argument above guarantees, used only by a debug assertion. If it ever
// trips, the span arithmetic is broken - it is never a data condition, and
// there is no alternative path to take.
constexpr std::size_t DEPTH_ASSERT_BOUND = 66;

} // namespace drs
