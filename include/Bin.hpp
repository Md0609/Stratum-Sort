#pragma once

#include <cstdint>
#include <limits>

namespace drs {

// ============================================================
// Bin
// ============================================================
// Represents a single interval produced during Phase 1 (analysis).
// During the first pass a Bin only accumulates statistics; it does
// NOT store the actual elements yet (those are only materialized
// in the second pass, see DynamicRangeSort::FinalBin).
// ============================================================
template <typename T>
struct Bin {
    T lowerBound{};
    T upperBound{};
    std::size_t count = 0;
    T observedMin = std::numeric_limits<T>::max();
    T observedMax = std::numeric_limits<T>::min();
    bool needsSubdivision = false;

    // Updates the running statistics with a newly-seen value.
    // O(1) per call, as required by the specification.
    void updateObserved(T value) {
        if (value < observedMin) observedMin = value;
        if (value > observedMax) observedMax = value;
        ++count;
    }

    bool isEmpty() const {
        return count == 0;
    }
};

} // namespace drs
