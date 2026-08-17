#pragma once

// ============================================================
// DynamicRangeSortV1 - historical reconstruction for comparison
// ============================================================
// Preserves the exact algorithmic behavior of the first working version
// of Stratum Sort: a single, non-recursive subdivision level, targetElementsPerBin
// = 16, and no ascending/descending run detection before local sorting.
// Metrics instrumentation is intentionally omitted here (this exists only
// to be timed and compared against the current algorithm, not to
// reproduce historical SortMetrics output byte-for-byte).
// ============================================================

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace stratum::v1 {

template <typename T>
struct Bin {
    T lowerBound{};
    T upperBound{};
    std::size_t count = 0;
    T observedMin = std::numeric_limits<T>::max();
    T observedMax = std::numeric_limits<T>::min();
    bool needsSubdivision = false;

    void updateObserved(T value) {
        if (value < observedMin) observedMin = value;
        if (value > observedMax) observedMax = value;
        ++count;
    }
    bool isEmpty() const { return count == 0; }
};

template <typename T>
class DynamicRangeSortV1 {
    static_assert(std::is_integral<T>::value, "requires integral type");

public:
    explicit DynamicRangeSortV1(std::size_t targetElementsPerBin = 16)
        : target_(targetElementsPerBin) {}

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
        uint64_t intervalSize = (range + initialBins - 1) / initialBins;
        if (intervalSize == 0) intervalSize = 1;

        std::vector<Bin<T>> bins(initialBins);
        for (std::size_t i = 0; i < initialBins; ++i) {
            const uint64_t lower = static_cast<uint64_t>(minV) + i * intervalSize;
            bins[i].lowerBound = static_cast<T>(lower);
            bins[i].upperBound = static_cast<T>(lower + intervalSize - 1);
        }

        auto binIndex = [&](T value) -> std::size_t {
            const uint64_t off = static_cast<uint64_t>(value) - static_cast<uint64_t>(minV);
            std::size_t idx = static_cast<std::size_t>(off / intervalSize);
            if (idx >= initialBins) idx = initialBins - 1;
            return idx;
        };

        for (T v : data) bins[binIndex(v)].updateObserved(v);

        struct SubInfo {
            bool needsSub = false;
            T observedMin{};
            T newIntervalSize{};
            std::size_t splits = 1;
            std::size_t offset = 0;
        };
        std::vector<SubInfo> subInfo(bins.size());
        std::size_t finalCount = 0;
        for (std::size_t i = 0; i < bins.size(); ++i) {
            Bin<T>& b = bins[i];
            SubInfo& s = subInfo[i];
            s.offset = finalCount;
            if (b.isEmpty() || b.count <= target_) {
                s.splits = 1;
                finalCount += 1;
                continue;
            }
            b.needsSubdivision = true;
            s.needsSub = true;
            const std::size_t splits = (b.count + target_ - 1) / target_;
            const uint64_t observedRange =
                static_cast<uint64_t>(b.observedMax) - static_cast<uint64_t>(b.observedMin) + 1ULL;
            uint64_t newInterval = (observedRange + splits - 1) / splits;
            if (newInterval == 0) newInterval = 1;
            s.observedMin = b.observedMin;
            s.newIntervalSize = static_cast<T>(newInterval);
            s.splits = splits;
            finalCount += splits;
        }

        std::vector<std::vector<T>> finalBins(finalCount);
        for (T v : data) {
            const std::size_t orig = binIndex(v);
            const SubInfo& s = subInfo[orig];
            std::size_t finalIdx = s.offset;
            if (s.needsSub) {
                const uint64_t off = static_cast<uint64_t>(v) - static_cast<uint64_t>(s.observedMin);
                std::size_t sub = static_cast<std::size_t>(off / static_cast<uint64_t>(s.newIntervalSize));
                if (sub >= s.splits) sub = s.splits - 1;
                finalIdx += sub;
            }
            finalBins[finalIdx].push_back(v);
        }

        for (auto& fb : finalBins) sortLocal(fb);

        std::size_t pos = 0;
        for (auto& fb : finalBins) {
            for (T v : fb) data[pos++] = v;
        }
    }

private:
    std::size_t target_;

    void sortLocal(std::vector<T>& arr) {
        if (arr.size() <= 16) {
            insertionSort(arr, 0, static_cast<long>(arr.size()) - 1);
        } else if (arr.size() <= 100) {
            quickSort(arr, 0, static_cast<long>(arr.size()) - 1);
        } else {
            std::sort(arr.begin(), arr.end()); // introsort-equivalent for the reconstruction
        }
    }

    void insertionSort(std::vector<T>& arr, long left, long right) {
        for (long i = left + 1; i <= right; ++i) {
            T key = arr[i];
            long j = i - 1;
            while (j >= left && arr[j] > key) {
                arr[j + 1] = arr[j];
                --j;
            }
            arr[j + 1] = key;
        }
    }

    void quickSort(std::vector<T>& arr, long left, long right) {
        if (left >= right) return;
        long mid = left + (right - left) / 2;
        T pivot = arr[mid];
        long i = left, j = right;
        while (i <= j) {
            while (arr[i] < pivot) ++i;
            while (arr[j] > pivot) --j;
            if (i <= j) { std::swap(arr[i], arr[j]); ++i; --j; }
        }
        quickSort(arr, left, j);
        quickSort(arr, i, right);
    }
};

} // namespace stratum::v1
