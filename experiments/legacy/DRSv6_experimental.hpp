#pragma once

// ============================================================
// DynamicRangeSortV6 - historical reconstruction, isolated from production
// ============================================================
// Preserves v6's full behavior, including the three experimental
// heuristics that were measured and found net-negative
// (documentacion/ANALYSIS_v6.md): micro-histogram splits, density-aware
// initial bins, and Difficulty-Score-based subdivision skipping. None of
// this participates in the production algorithm (algoritmo/
// DynamicRangeSort.hpp) as of v7 - it exists only so the v3-v7
// comparison in ANALYSIS_v7.md has a genuine v6 data point, and so the
// disproven heuristics remain inspectable without cluttering the
// production code path with dead branches.
//
// This is a self-contained, simplified reconstruction (no DRSMetrics
// dependency) - faithful to the v6 algorithmic behavior for benchmarking
// purposes, not a copy of the exact historical source.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace drs::v6 {

constexpr std::size_t kMaxDepth = 6;
constexpr std::size_t kMicroHistogramBuckets = 16;
constexpr std::size_t kDensityMapBuckets = 2048;
constexpr double kPresortednessSkipThreshold = 0.90;

template <typename T>
class DynamicRangeSortV6 {
    static_assert(std::is_integral<T>::value, "requires integral type");

public:
    explicit DynamicRangeSortV6(std::size_t target = 64, bool useMicroHistogram = false,
                                 bool useDensityAwareBins = false, bool useDifficultyScoreSkip = false)
        : target_(target),
          useMicroHistogram_(useMicroHistogram),
          useDensityAwareBins_(useDensityAwareBins),
          useDifficultyScoreSkip_(useDifficultyScoreSkip) {}

    void sort(std::vector<T>& data) {
        if (data.size() < 2) return;

        T minV = data[0], maxV = data[0];
        for (T v : data) {
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }

        const uint64_t range = static_cast<uint64_t>(maxV) - static_cast<uint64_t>(minV) + 1ULL;
        std::size_t initialBins = (data.size() + target_ - 1) / target_;
        if (initialBins == 0) initialBins = 1;
        const std::size_t rangeAsSize =
            range > std::numeric_limits<std::size_t>::max() ? std::numeric_limits<std::size_t>::max()
                                                              : static_cast<std::size_t>(range);
        initialBins = std::min(initialBins, rangeAsSize);
        if (initialBins == 0) initialBins = 1;

        bufferA_.assign(data.size(), T{});
        bufferB_.assign(data.size(), T{});

        std::vector<std::size_t> bucketStart, bucketSize;
        if (useDensityAwareBins_) {
            const auto densityMap = buildDensityMap(data, minV, maxV);
            const auto boundaries = computeDensityAwareBoundaries(densityMap, minV, maxV, initialBins);
            distributeWithBoundaries(data, boundaries, bucketStart, bucketSize);
        } else {
            uint64_t intervalSize = (range + initialBins - 1) / initialBins;
            if (intervalSize == 0) intervalSize = 1;
            distributeUniform(data, minV, static_cast<T>(intervalSize), initialBins, bucketStart,
                               bucketSize);
        }

        std::vector<Node> roots;
        roots.reserve(bucketStart.size());
        for (std::size_t i = 0; i < bucketStart.size(); ++i) {
            roots.push_back(refine(true, bucketStart[i], bucketSize[i], 0));
        }
        for (auto& r : roots) sortNode(r);

        std::size_t pos = 0;
        for (auto& r : roots) mergeNode(r, data, pos);
    }

private:
    std::size_t target_;
    bool useMicroHistogram_;
    bool useDensityAwareBins_;
    bool useDifficultyScoreSkip_;
    std::vector<T> bufferA_, bufferB_;

    struct Node {
        bool inBufferA = true;
        std::size_t start = 0, count = 0;
        std::vector<Node> children;
        bool isLeaf() const { return children.empty(); }
    };

    static std::size_t binIndex(T value, T start, uint64_t step, std::size_t count) {
        const uint64_t off = static_cast<uint64_t>(value) - static_cast<uint64_t>(start);
        std::size_t idx = static_cast<std::size_t>(off / step);
        if (idx >= count) idx = count - 1;
        return idx;
    }

    void distributeUniform(const std::vector<T>& data, T minV, T intervalSize, std::size_t binCount,
                            std::vector<std::size_t>& outStart, std::vector<std::size_t>& outSize) {
        outSize.assign(binCount, 0);
        std::vector<std::size_t> bucketOf(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            const std::size_t idx = binIndex(data[i], minV, static_cast<uint64_t>(intervalSize), binCount);
            bucketOf[i] = idx;
            ++outSize[idx];
        }
        outStart.assign(binCount, 0);
        std::vector<std::size_t> cursor(binCount);
        std::size_t c = 0;
        for (std::size_t b = 0; b < binCount; ++b) { outStart[b] = c; cursor[b] = c; c += outSize[b]; }
        for (std::size_t i = 0; i < data.size(); ++i) bufferA_[cursor[bucketOf[i]]++] = data[i];
    }

    std::vector<std::size_t> buildDensityMap(const std::vector<T>& data, T minV, T maxV) const {
        const uint64_t range = static_cast<uint64_t>(maxV) - static_cast<uint64_t>(minV) + 1ULL;
        std::size_t buckets = std::min<std::size_t>(kDensityMapBuckets, static_cast<std::size_t>(range));
        if (buckets == 0) buckets = 1;
        uint64_t step = (range + buckets - 1) / buckets;
        if (step == 0) step = 1;
        std::vector<std::size_t> map(buckets, 0);
        for (T v : data) ++map[binIndex(v, minV, step, buckets)];
        return map;
    }

    std::vector<T> computeDensityAwareBoundaries(const std::vector<std::size_t>& densityMap, T minV,
                                                  T maxV, std::size_t binCount) const {
        const std::size_t mapBuckets = densityMap.size();
        const uint64_t range = static_cast<uint64_t>(maxV) - static_cast<uint64_t>(minV) + 1ULL;
        uint64_t mapStep = (range + mapBuckets - 1) / mapBuckets;
        if (mapStep == 0) mapStep = 1;

        std::vector<std::size_t> cumulative(mapBuckets + 1, 0);
        for (std::size_t b = 0; b < mapBuckets; ++b) cumulative[b + 1] = cumulative[b] + densityMap[b];
        const std::size_t total = cumulative[mapBuckets];

        auto boundaryValue = [&](std::size_t b) {
            return static_cast<double>(minV) + static_cast<double>(b) * static_cast<double>(mapStep);
        };

        std::vector<T> boundaries(binCount + 1);
        boundaries[0] = minV;
        boundaries[binCount] = maxV;
        for (std::size_t s = 1; s < binCount; ++s) {
            const double targetRank = static_cast<double>(s) * total / static_cast<double>(binCount);
            std::size_t b = 0;
            while (b < mapBuckets && static_cast<double>(cumulative[b + 1]) < targetRank) ++b;
            if (b >= mapBuckets) b = mapBuckets - 1;
            const double lo = boundaryValue(b), hi = boundaryValue(b + 1);
            const double cLo = static_cast<double>(cumulative[b]), cHi = static_cast<double>(cumulative[b + 1]);
            double frac = (cHi > cLo) ? (targetRank - cLo) / (cHi - cLo) : 0.0;
            frac = std::clamp(frac, 0.0, 1.0);
            boundaries[s] = static_cast<T>(
                std::clamp(lo + frac * (hi - lo), static_cast<double>(minV), static_cast<double>(maxV)));
        }
        for (std::size_t s = 1; s <= binCount; ++s)
            if (boundaries[s] < boundaries[s - 1]) boundaries[s] = boundaries[s - 1];
        return boundaries;
    }

    std::size_t findBoundaryBin(T value, const std::vector<T>& boundaries) const {
        const std::size_t binCount = boundaries.size() - 1;
        if (binCount <= 1) return 0;
        const auto it = std::upper_bound(boundaries.begin() + 1, boundaries.end() - 1, value);
        std::size_t idx = static_cast<std::size_t>(it - (boundaries.begin() + 1));
        if (idx >= binCount) idx = binCount - 1;
        return idx;
    }

    void distributeWithBoundaries(const std::vector<T>& data, const std::vector<T>& boundaries,
                                   std::vector<std::size_t>& outStart, std::vector<std::size_t>& outSize) {
        const std::size_t binCount = boundaries.size() - 1;
        outSize.assign(binCount, 0);
        std::vector<std::size_t> bucketOf(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            const std::size_t idx = findBoundaryBin(data[i], boundaries);
            bucketOf[i] = idx;
            ++outSize[idx];
        }
        outStart.assign(binCount, 0);
        std::vector<std::size_t> cursor(binCount);
        std::size_t c = 0;
        for (std::size_t b = 0; b < binCount; ++b) { outStart[b] = c; cursor[b] = c; c += outSize[b]; }
        for (std::size_t i = 0; i < data.size(); ++i) bufferA_[cursor[bucketOf[i]]++] = data[i];
    }

    Node refine(bool inBufferA, std::size_t start, std::size_t count, std::size_t depth) {
        Node node;
        node.inBufferA = inBufferA;
        node.start = start;
        node.count = count;
        if (count == 0) return node;

        std::vector<T>& cur = inBufferA ? bufferA_ : bufferB_;
        if (count <= target_ || depth >= kMaxDepth) return node;

        T observedMin = cur[start], observedMax = cur[start];
        std::size_t asc = 0, desc = 0;
        for (std::size_t i = start; i < start + count; ++i) {
            if (cur[i] < observedMin) observedMin = cur[i];
            if (cur[i] > observedMax) observedMax = cur[i];
            if (i > start) { if (cur[i] >= cur[i - 1]) ++asc; else ++desc; }
        }
        if (observedMin == observedMax) return node;

        if (useDifficultyScoreSkip_ && count > 1) {
            const double presorted =
                static_cast<double>(std::max(asc, desc)) / static_cast<double>(count - 1);
            if (presorted >= kPresortednessSkipThreshold) return node;
        }

        const std::size_t splits = (count + target_ - 1) / target_;
        const uint64_t observedRange =
            static_cast<uint64_t>(observedMax) - static_cast<uint64_t>(observedMin) + 1ULL;

        std::vector<T>& other = inBufferA ? bufferB_ : bufferA_;
        std::vector<std::size_t> bucketStart, bucketSize;

        if (useMicroHistogram_ && splits <= kMicroHistogramBuckets) {
            countAndPlaceHistogram(cur, start, count, other, start, observedMin, observedRange, splits,
                                    bucketStart, bucketSize);
        } else {
            uint64_t step = (observedRange + splits - 1) / splits;
            if (step == 0) step = 1;
            countAndPlace(cur, start, count, other, start, observedMin, static_cast<T>(step), splits,
                          bucketStart, bucketSize);
        }

        node.children.reserve(splits);
        for (std::size_t s = 0; s < splits; ++s)
            node.children.push_back(refine(!inBufferA, bucketStart[s], bucketSize[s], depth + 1));
        return node;
    }

    void countAndPlace(const std::vector<T>& src, std::size_t srcStart, std::size_t count,
                        std::vector<T>& dst, std::size_t dstStart, T rangeStart, T step,
                        std::size_t numBuckets, std::vector<std::size_t>& outStart,
                        std::vector<std::size_t>& outSize) const {
        outSize.assign(numBuckets, 0);
        std::vector<std::size_t> bucketOf(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t idx = binIndex(src[srcStart + i], rangeStart, static_cast<uint64_t>(step), numBuckets);
            bucketOf[i] = idx;
            ++outSize[idx];
        }
        outStart.assign(numBuckets, 0);
        std::vector<std::size_t> cursor(numBuckets);
        std::size_t c = dstStart;
        for (std::size_t b = 0; b < numBuckets; ++b) { outStart[b] = c; cursor[b] = c; c += outSize[b]; }
        for (std::size_t i = 0; i < count; ++i) dst[cursor[bucketOf[i]]++] = src[srcStart + i];
    }

    void countAndPlaceHistogram(const std::vector<T>& src, std::size_t srcStart, std::size_t count,
                                 std::vector<T>& dst, std::size_t dstStart, T rangeStart,
                                 uint64_t observedRange, std::size_t numBuckets,
                                 std::vector<std::size_t>& outStart, std::vector<std::size_t>& outSize) {
        const std::size_t histBuckets = kMicroHistogramBuckets;
        uint64_t histStep = (observedRange + histBuckets - 1) / histBuckets;
        if (histStep == 0) histStep = 1;
        std::vector<std::size_t> hist(histBuckets, 0);
        for (std::size_t i = 0; i < count; ++i)
            ++hist[binIndex(src[srcStart + i], rangeStart, histStep, histBuckets)];
        std::vector<std::size_t> cumulative(histBuckets + 1, 0);
        for (std::size_t b = 0; b < histBuckets; ++b) cumulative[b + 1] = cumulative[b] + hist[b];

        auto boundaryValue = [&](std::size_t b) {
            return static_cast<double>(rangeStart) + static_cast<double>(b) * static_cast<double>(histStep);
        };
        std::vector<double> inner(numBuckets > 0 ? numBuckets - 1 : 0);
        for (std::size_t s = 1; s < numBuckets; ++s) {
            const double targetRank = static_cast<double>(s) * count / static_cast<double>(numBuckets);
            std::size_t b = 0;
            while (b < histBuckets && static_cast<double>(cumulative[b + 1]) < targetRank) ++b;
            if (b >= histBuckets) b = histBuckets - 1;
            const double lo = boundaryValue(b), hi = boundaryValue(b + 1);
            const double cLo = static_cast<double>(cumulative[b]), cHi = static_cast<double>(cumulative[b + 1]);
            double frac = (cHi > cLo) ? (targetRank - cLo) / (cHi - cLo) : 0.0;
            frac = std::clamp(frac, 0.0, 1.0);
            inner[s - 1] = lo + frac * (hi - lo);
        }

        outSize.assign(numBuckets, 0);
        std::vector<std::size_t> bucketOf(count);
        for (std::size_t i = 0; i < count; ++i) {
            const double value = static_cast<double>(src[srcStart + i]);
            std::size_t b = static_cast<std::size_t>(std::upper_bound(inner.begin(), inner.end(), value) - inner.begin());
            if (b >= numBuckets) b = numBuckets - 1;
            bucketOf[i] = b;
            ++outSize[b];
        }
        outStart.assign(numBuckets, 0);
        std::vector<std::size_t> cursor(numBuckets);
        std::size_t c = dstStart;
        for (std::size_t b = 0; b < numBuckets; ++b) { outStart[b] = c; cursor[b] = c; c += outSize[b]; }
        for (std::size_t i = 0; i < count; ++i) dst[cursor[bucketOf[i]]++] = src[srcStart + i];
    }

    enum class Run { Ascending, Descending, Unsorted };

    Run detectRun(const std::vector<T>& arr, std::size_t start, std::size_t count) {
        if (count < 2) return Run::Ascending;
        bool asc = true, desc = true;
        for (std::size_t i = start + 1; i < start + count; ++i) {
            if (arr[i] < arr[i - 1]) asc = false;
            if (arr[i] > arr[i - 1]) desc = false;
            if (!asc && !desc) return Run::Unsorted;
        }
        return asc ? Run::Ascending : Run::Descending;
    }

    void sortLeaf(std::vector<T>& buf, std::size_t start, std::size_t count) {
        if (count < 2) return;
        switch (detectRun(buf, start, count)) {
            case Run::Ascending: return;
            case Run::Descending:
                std::reverse(buf.begin() + static_cast<long>(start), buf.begin() + static_cast<long>(start + count));
                return;
            case Run::Unsorted: break;
        }
        const long left = static_cast<long>(start), right = static_cast<long>(start + count - 1);
        std::sort(buf.begin() + left, buf.begin() + right + 1);
    }

    void sortNode(Node& node) {
        if (node.isLeaf()) { sortLeaf(node.inBufferA ? bufferA_ : bufferB_, node.start, node.count); return; }
        for (auto& c : node.children) sortNode(c);
    }

    void mergeNode(const Node& node, std::vector<T>& out, std::size_t& pos) {
        if (node.isLeaf()) {
            const auto& buf = node.inBufferA ? bufferA_ : bufferB_;
            for (std::size_t i = node.start; i < node.start + node.count; ++i) out[pos++] = buf[i];
            return;
        }
        for (auto& c : node.children) mergeNode(c, out, pos);
    }
};

} // namespace drs::v6
