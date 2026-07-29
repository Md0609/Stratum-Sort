#pragma once

// ============================================================
// DynamicRangeSortV3 - historical reconstruction for comparison
// ============================================================
// Preserves v3's behavior: the old separate statistics pass and grouping
// pass fused into one distribute() pass (push_back based, no pre-reserve
// at the top level), refine() building child buckets with a
// count-then-place scheme (reserve then place, no reallocation churn),
// targetElementsPerBin retuned to 64, run detection retained from v2.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace drs::v3 {

constexpr std::size_t kMaxDepth = 6;

template <typename T>
class DynamicRangeSortV3 {
    static_assert(std::is_integral<T>::value, "requires integral type");

public:
    explicit DynamicRangeSortV3(std::size_t targetElementsPerBin = 64) : target_(targetElementsPerBin) {}

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
        uint64_t intervalSize = (range + initialBins - 1) / initialBins;
        if (intervalSize == 0) intervalSize = 1;

        auto binIndex = [](T value, T start, uint64_t step, std::size_t count) -> std::size_t {
            const uint64_t off = static_cast<uint64_t>(value) - static_cast<uint64_t>(start);
            std::size_t idx = static_cast<std::size_t>(off / step);
            if (idx >= count) idx = count - 1;
            return idx;
        };

        // distribute(): fused stats+placement pass (push_back, no pre-reserve).
        std::vector<std::vector<T>> groups(initialBins);
        for (T v : data) groups[binIndex(v, minV, intervalSize, initialBins)].push_back(v);

        std::vector<Node> roots;
        roots.reserve(initialBins);
        for (auto& g : groups) roots.push_back(refine(std::move(g), 0));

        for (auto& r : roots) sortNode(r);

        std::size_t pos = 0;
        for (auto& r : roots) mergeNode(r, data, pos);
    }

private:
    std::size_t target_;

    struct Node {
        std::vector<T> elements;
        std::vector<Node> children;
        bool isLeaf() const { return children.empty(); }
    };

    Node refine(std::vector<T>&& elements, std::size_t depth) {
        Node node;
        const std::size_t count = elements.size();
        if (count == 0) return node;
        if (count <= target_ || depth >= kMaxDepth) {
            node.elements = std::move(elements);
            return node;
        }

        T observedMin = elements[0], observedMax = elements[0];
        for (T v : elements) {
            if (v < observedMin) observedMin = v;
            if (v > observedMax) observedMax = v;
        }
        if (observedMin == observedMax) {
            node.elements = std::move(elements);
            return node;
        }

        const std::size_t splits = (count + target_ - 1) / target_;
        const uint64_t observedRange =
            static_cast<uint64_t>(observedMax) - static_cast<uint64_t>(observedMin) + 1ULL;
        uint64_t newInterval = (observedRange + splits - 1) / splits;
        if (newInterval == 0) newInterval = 1;

        // count-then-place: no reallocation churn while filling buckets.
        std::vector<std::size_t> bucketOf(count);
        std::vector<std::size_t> bucketSizes(splits, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t off = static_cast<uint64_t>(elements[i]) - static_cast<uint64_t>(observedMin);
            std::size_t idx = static_cast<std::size_t>(off / newInterval);
            if (idx >= splits) idx = splits - 1;
            bucketOf[i] = idx;
            ++bucketSizes[idx];
        }
        std::vector<std::vector<T>> buckets(splits);
        for (std::size_t s = 0; s < splits; ++s) buckets[s].reserve(bucketSizes[s]);
        for (std::size_t i = 0; i < count; ++i) buckets[bucketOf[i]].push_back(elements[i]);

        elements.clear();
        elements.shrink_to_fit();

        node.children.reserve(splits);
        for (auto& b : buckets) node.children.push_back(refine(std::move(b), depth + 1));
        return node;
    }

    enum class Run { Ascending, Descending, Unsorted };

    Run detectRun(const std::vector<T>& arr) {
        if (arr.size() < 2) return Run::Ascending;
        bool asc = true, desc = true;
        for (std::size_t i = 1; i < arr.size(); ++i) {
            if (arr[i] < arr[i - 1]) asc = false;
            if (arr[i] > arr[i - 1]) desc = false;
            if (!asc && !desc) return Run::Unsorted;
        }
        return asc ? Run::Ascending : Run::Descending;
    }

    void sortLeaf(std::vector<T>& arr) {
        if (arr.size() < 2) return;
        switch (detectRun(arr)) {
            case Run::Ascending: return;
            case Run::Descending: std::reverse(arr.begin(), arr.end()); return;
            case Run::Unsorted: break;
        }
        if (arr.size() <= 64) {
            insertionSort(arr);
        } else if (arr.size() <= 384) {
            quickSort(arr, 0, static_cast<long>(arr.size()) - 1);
        } else {
            std::sort(arr.begin(), arr.end());
        }
    }

    void insertionSort(std::vector<T>& arr) {
        for (std::size_t i = 1; i < arr.size(); ++i) {
            T key = arr[i];
            long j = static_cast<long>(i) - 1;
            while (j >= 0 && arr[j] > key) {
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

    void sortNode(Node& node) {
        if (node.isLeaf()) { sortLeaf(node.elements); return; }
        for (auto& c : node.children) sortNode(c);
    }

    void mergeNode(const Node& node, std::vector<T>& out, std::size_t& pos) {
        if (node.isLeaf()) {
            for (T v : node.elements) out[pos++] = v;
            return;
        }
        for (auto& c : node.children) mergeNode(c, out, pos);
    }
};

} // namespace drs::v3
