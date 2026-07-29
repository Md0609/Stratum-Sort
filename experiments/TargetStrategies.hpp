#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace drs::experimental {

// ============================================================
// Target strategies (EXPERIMENTAL - "TARGET DINAMICO" study)
// ============================================================
// These functions compute a candidate targetElementsPerBin value from n
// and/or the dataset's value range. They live entirely in the benchmark
// harness, not in DynamicRangeSort itself: the class already accepts
// targetElementsPerBin as a runtime constructor parameter (since v3), so
// comparing strategies only means computing a different number before
// construction - no core algorithm change is needed or made here.
//
// Every function returns at least 1 (a bin of size 0 is meaningless).
// ============================================================
struct TargetStrategy {
    std::string name;
    std::size_t (*compute)(std::size_t n, uint64_t range);
};

inline std::size_t targetFixed64(std::size_t /*n*/, uint64_t /*range*/) { return 64; }

inline std::size_t targetSqrtN(std::size_t n, uint64_t /*range*/) {
    const double v = std::sqrt(static_cast<double>(n));
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::size_t targetLog2N(std::size_t n, uint64_t /*range*/) {
    if (n < 2) return 1;
    const double v = std::log2(static_cast<double>(n));
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::size_t targetLog2N16x(std::size_t n, uint64_t /*range*/) {
    if (n < 2) return 16;
    const double v = 16.0 * std::log2(static_cast<double>(n));
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::size_t targetLog2N32x(std::size_t n, uint64_t /*range*/) {
    if (n < 2) return 32;
    const double v = 32.0 * std::log2(static_cast<double>(n));
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::size_t targetSqrtRange(std::size_t /*n*/, uint64_t range) {
    const double v = std::sqrt(static_cast<double>(range));
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::size_t targetSqrtRangeScaled(std::size_t /*n*/, uint64_t range) {
    // Scaling constant chosen only to land in the same order of magnitude
    // as the other candidates for the ranges used in this project's
    // benchmark datasets (roughly 10^5-10^6) - itself part of what the
    // sweep evaluates, not an assumption about which strategy wins.
    const double v = std::sqrt(static_cast<double>(range)) * 4.0;
    return v < 1.0 ? 1 : static_cast<std::size_t>(v);
}

inline std::vector<TargetStrategy> allTargetStrategies() {
    return {
        {"Fixed64", targetFixed64},
        {"sqrt(n)", targetSqrtN},
        {"log2(n)", targetLog2N},
        {"16*log2(n)", targetLog2N16x},
        {"32*log2(n)", targetLog2N32x},
        {"sqrt(range)", targetSqrtRange},
        {"sqrt(range)*4", targetSqrtRangeScaled},
    };
}

} // namespace drs::experimental
