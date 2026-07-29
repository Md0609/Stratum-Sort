#pragma once

#include "Statistics.hpp"

#include <cmath>
#include <iomanip>
#include <map>
#include <ostream>
#include <vector>

namespace drs::experimental {

// ============================================================
// Bin size histogram (v5, section 3 of the research brief)
// ============================================================
// Bin sizes span from 0 (empty bins) to potentially hundreds of
// thousands of elements in degenerate cases (e.g. massive duplicate
// clusters), so an exact per-value frequency table would be unreadably
// long. Sizes are grouped into power-of-two buckets ([0], [1], [2,3],
// [4,7], [8,15], ...) for the printed table; median, mode and
// percentiles are computed from the exact, ungrouped values.
// ============================================================
inline void printBinSizeHistogram(const std::vector<std::size_t>& binSizes, std::ostream& os) {
    if (binSizes.empty()) {
        os << "No hay bins registrados.\n";
        return;
    }

    // Power-of-two bucket for a size: bucket 0 = {0}, bucket 1 = {1},
    // bucket k (k>=2) = [2^(k-1), 2^k - 1].
    auto bucketOf = [](std::size_t size) -> std::size_t {
        if (size == 0) return 0;
        if (size == 1) return 1;
        std::size_t bucket = 1;
        std::size_t upper = 1;
        while (upper < size) {
            upper = upper * 2 + 1;
            ++bucket;
        }
        return bucket;
    };
    auto bucketLabel = [](std::size_t bucket) -> std::string {
        if (bucket == 0) return "0";
        if (bucket == 1) return "1";
        const std::size_t lo = std::size_t{1} << (bucket - 1);
        const std::size_t hi = (std::size_t{1} << bucket) - 1;
        return "[" + std::to_string(lo) + "-" + std::to_string(hi) + "]";
    };

    std::map<std::size_t, std::size_t> freq;
    for (std::size_t s : binSizes) ++freq[bucketOf(s)];

    const std::size_t total = binSizes.size();
    os << std::left << std::setw(16) << "Rango de tamano" << std::right << std::setw(12) << "Frecuencia"
       << std::setw(12) << "Porcentaje" << "\n";
    os << std::string(40, '-') << "\n";
    for (const auto& [bucket, count] : freq) {
        const double pct = 100.0 * static_cast<double>(count) / static_cast<double>(total);
        os << std::left << std::setw(16) << bucketLabel(bucket) << std::right << std::setw(12) << count
           << std::setw(11) << std::fixed << std::setprecision(2) << pct << "%\n";
    }

    const auto summary = drs::stats::summarizeDistribution(binSizes);
    os << "\n";
    os << "Total de bins:      " << summary.count << "\n";
    os << "Mediana:            " << summary.median << "\n";
    os << "Moda:               " << summary.mode << " (aparece " << summary.modeFrequency
       << " veces)\n";
    os << "Percentil 90:       " << summary.p90 << "\n";
    os << "Percentil 95:       " << summary.p95 << "\n";
    os << "Percentil 99:       " << summary.p99 << "\n";
}

} // namespace drs::experimental
