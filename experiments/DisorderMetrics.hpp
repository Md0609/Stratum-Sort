#pragma once

#include "drs/DynamicRangeSort.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <vector>

namespace drs::experimental {

// ============================================================
// Disorder metrics (v5, section 4 of the research brief)
// ============================================================
// Measures how much internal order is already present inside the bins
// DRS produces, BEFORE any local sort runs - using DynamicRangeSort's
// read-only debugPartitionOnly() hook, so the algorithm itself is not
// touched or slowed down by this instrumentation.
//
// Definitions (chosen to be simple, unambiguous, and directly computable
// in a single O(k) pass per bin, not to match any particular textbook's
// run-length-encoding convention):
//   - Every adjacent pair (i-1, i) within a bin is classified as an
//     "ascending step" (element[i] >= element[i-1]) or a "descending
//     step" (element[i] < element[i-1]).
//   - A "run" is a maximal sequence of consecutive same-classified steps.
//     Its length is the number of elements it spans (steps + 1); two
//     consecutive runs share their one boundary element, which is
//     expected and does not double-count anything relevant here.
//   - "consecutive inversions" = number of descending steps.
//   - "percentage already sorted" = percentage of steps that are
//     ascending steps.
// ============================================================
struct DisorderStats {
    std::size_t totalBins = 0;
    std::size_t totalElements = 0;
    std::size_t ascendingRuns = 0;
    std::size_t descendingRuns = 0;
    std::size_t totalRunLength = 0;
    std::size_t totalRuns = 0;
    std::size_t maxRunLength = 0;
    std::size_t ascendingSteps = 0;
    std::size_t descendingSteps = 0; // == consecutive inversions

    double meanRunLength() const {
        return totalRuns == 0 ? 0.0
                               : static_cast<double>(totalRunLength) / static_cast<double>(totalRuns);
    }
    std::size_t totalSteps() const { return ascendingSteps + descendingSteps; }
    double percentAlreadySorted() const {
        const std::size_t steps = totalSteps();
        return steps == 0 ? 100.0 : 100.0 * static_cast<double>(ascendingSteps) / static_cast<double>(steps);
    }
};

template <typename T>
DisorderStats measureDisorder(drs::DynamicRangeSort<T>& sorter, const std::vector<T>& data) {
    DisorderStats stats;
    const auto leaves = sorter.debugPartitionOnly(data);
    stats.totalBins = leaves.size();

    for (const auto& leaf : leaves) {
        const std::vector<T>& buf = leaf.inBufferA ? sorter.debugBufferA() : sorter.debugBufferB();
        const std::size_t start = leaf.start;
        const std::size_t count = leaf.count;
        stats.totalElements += count;
        if (count < 2) {
            if (count == 1) {
                stats.totalRuns += 1;
                stats.totalRunLength += 1;
                stats.maxRunLength = std::max(stats.maxRunLength, std::size_t{1});
                stats.ascendingRuns += 1;
            }
            continue;
        }

        bool currentAscending = buf[start + 1] >= buf[start];
        std::size_t runStart = start;

        for (std::size_t i = start + 1; i < start + count; ++i) {
            const bool stepAscending = buf[i] >= buf[i - 1];
            if (stepAscending) {
                ++stats.ascendingSteps;
            } else {
                ++stats.descendingSteps;
            }

            if (stepAscending != currentAscending) {
                const std::size_t runLen = i - runStart; // elements spanned so far
                stats.totalRuns += 1;
                stats.totalRunLength += runLen;
                stats.maxRunLength = std::max(stats.maxRunLength, runLen);
                if (currentAscending) ++stats.ascendingRuns; else ++stats.descendingRuns;
                runStart = i - 1;
                currentAscending = stepAscending;
            }
        }
        const std::size_t runLen = (start + count) - runStart;
        stats.totalRuns += 1;
        stats.totalRunLength += runLen;
        stats.maxRunLength = std::max(stats.maxRunLength, runLen);
        if (currentAscending) ++stats.ascendingRuns; else ++stats.descendingRuns;
    }

    return stats;
}

inline void printDisorderStats(const DisorderStats& s, std::ostream& os) {
    os << "  Bins analizados:            " << s.totalBins << "\n";
    os << "  Elementos analizados:       " << s.totalElements << "\n";
    os << "  Runs ascendentes:           " << s.ascendingRuns << "\n";
    os << "  Runs descendentes:          " << s.descendingRuns << "\n";
    os << "  Longitud media de run:      " << std::fixed << std::setprecision(3) << s.meanRunLength()
       << "\n";
    os << "  Longitud maxima de run:     " << s.maxRunLength << "\n";
    os << "  % de pasos ya ordenados:    " << std::fixed << std::setprecision(2)
       << s.percentAlreadySorted() << "%\n";
    os << "  Inversiones consecutivas:   " << s.descendingSteps << "\n";
}

} // namespace drs::experimental
