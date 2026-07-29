#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace drs::experimental {

// ============================================================
// Locality experiment (v5, section 5 of the research brief)
// ============================================================
// Two functions doing IDENTICAL work (same counting-sort-style
// count-then-place distribution DynamicRangeSort::distribute() uses),
// differing only in loop structure: the unblocked version walks the
// source array in one flat pass for counting and one flat pass for
// placement (exactly what the production distribute() does); the
// blocked version does the same two passes but grouped into outer loops
// over consecutive blocks of 'blockSize' elements. This isolates the
// effect of processing granularity alone - no different memory access
// order, no different algorithm. This module is standalone and does not
// modify DynamicRangeSort or distribute() itself.
// ============================================================

template <typename T>
std::size_t localityBinIndex(T value, T rangeStart, T intervalSize, std::size_t numBuckets) {
    const uint64_t offset = static_cast<uint64_t>(value) - static_cast<uint64_t>(rangeStart);
    std::size_t idx = static_cast<std::size_t>(offset / static_cast<uint64_t>(intervalSize));
    if (idx >= numBuckets) idx = numBuckets - 1;
    return idx;
}

template <typename T>
void distributeUnblocked(const std::vector<T>& data, T minV, T intervalSize, std::size_t numBuckets,
                          std::vector<T>& dst) {
    const std::size_t n = data.size();
    std::vector<std::size_t> bucketOf(n);
    std::vector<std::size_t> bucketSize(numBuckets, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t idx = localityBinIndex(data[i], minV, intervalSize, numBuckets);
        bucketOf[i] = idx;
        ++bucketSize[idx];
    }
    std::vector<std::size_t> cursor(numBuckets);
    std::size_t c = 0;
    for (std::size_t b = 0; b < numBuckets; ++b) {
        cursor[b] = c;
        c += bucketSize[b];
    }
    dst.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        dst[cursor[bucketOf[i]]++] = data[i];
    }
}

template <typename T>
void distributeBlocked(const std::vector<T>& data, T minV, T intervalSize, std::size_t numBuckets,
                        std::size_t blockSize, std::vector<T>& dst) {
    const std::size_t n = data.size();
    std::vector<std::size_t> bucketOf(n);
    std::vector<std::size_t> bucketSize(numBuckets, 0);
    for (std::size_t blockStart = 0; blockStart < n; blockStart += blockSize) {
        const std::size_t blockEnd = std::min(n, blockStart + blockSize);
        for (std::size_t i = blockStart; i < blockEnd; ++i) {
            const std::size_t idx = localityBinIndex(data[i], minV, intervalSize, numBuckets);
            bucketOf[i] = idx;
            ++bucketSize[idx];
        }
    }
    std::vector<std::size_t> cursor(numBuckets);
    std::size_t c = 0;
    for (std::size_t b = 0; b < numBuckets; ++b) {
        cursor[b] = c;
        c += bucketSize[b];
    }
    dst.resize(n);
    for (std::size_t blockStart = 0; blockStart < n; blockStart += blockSize) {
        const std::size_t blockEnd = std::min(n, blockStart + blockSize);
        for (std::size_t i = blockStart; i < blockEnd; ++i) {
            dst[cursor[bucketOf[i]]++] = data[i];
        }
    }
}

// Runs 'repetitions' trials of a distribute function and returns the
// median time in milliseconds.
template <typename Fn>
double medianTimeMs(Fn&& fn, std::size_t repetitions) {
    std::vector<double> samples;
    samples.reserve(repetitions);
    for (std::size_t r = 0; r < repetitions; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace drs::experimental
