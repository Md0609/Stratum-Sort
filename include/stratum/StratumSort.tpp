#pragma once

// Included at the bottom of StratumSort.hpp; not meant to be
// included directly. The algorithm itself is in detail/Engine.hpp; this
// file is the class around it.

#include <cassert>
#include <vector>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

// ============================================================
// Construction
// ============================================================
// Without arguments: automatic. The accessors report the small-input
// values; each sort resolves the pair for its own n.
template <typename T>
StratumSort<T>::StratumSort()
    : targetElementsPerBin_(DEFAULT_TARGET_ELEMENTS_PER_BIN),
      leafThreshold_(DEFAULT_LEAF_THRESHOLD),
      requested_{0, 0} {}

// Positional: 0.10.0's contract, clamped, fixed for every n. lambda == 0
// means the default value (16), t below lambda is raised to lambda.
template <typename T>
StratumSort<T>::StratumSort(std::size_t targetElementsPerBin, std::size_t leafThreshold)
    : targetElementsPerBin_(targetElementsPerBin == 0 ? DEFAULT_TARGET_ELEMENTS_PER_BIN
                            : targetElementsPerBin > MAX_TARGET_ELEMENTS_PER_BIN
                                ? MAX_TARGET_ELEMENTS_PER_BIN
                                : targetElementsPerBin),
      leafThreshold_(leafThreshold < targetElementsPerBin_ ? targetElementsPerBin_
                     : leafThreshold > MAX_LEAF_THRESHOLD  ? MAX_LEAF_THRESHOLD
                                                           : leafThreshold),
      requested_{targetElementsPerBin_, leafThreshold_} {
    assert(targetElementsPerBin_ >= 1 && targetElementsPerBin_ <= MAX_TARGET_ELEMENTS_PER_BIN);
    assert(leafThreshold_ >= targetElementsPerBin_ && leafThreshold_ <= MAX_LEAF_THRESHOLD);
}

// Named: a field left at 0 is automatic, a field set is clamped.
template <typename T>
StratumSort<T>::StratumSort(const Parameters& parameters)
    : targetElementsPerBin_(resolveParameters(parameters, 0).targetElementsPerBin),
      leafThreshold_(resolveParameters(parameters, 0).leafThreshold),
      requested_(parameters) {}

// ============================================================
// Sorting
// ============================================================
template <typename T>
void StratumSort<T>::sortRange(T* first, std::size_t n, Workspace<T>& workspace) const {
#ifdef STRATUM_ENABLE_METRICS
    workspace.metrics().reset();
    const detail::Probe probe(&workspace.metrics());
#else
    const detail::Probe probe;
#endif
    detail::SortLeaves sink;
    const Parameters p = resolveParameters(requested_, n);
    detail::sortWith</*Stable=*/false>(Traits{}, first, n, p.targetElementsPerBin, p.leafThreshold,
                                       workspace, probe, sink);
}

template <typename T>
void StratumSort<T>::sort(std::vector<T>& data) {
    sortRange(data.data(), data.size(), workspace_);
}

template <typename T>
void StratumSort<T>::sort(T* first, T* last) {
    assert(first <= last);
    sortRange(first, static_cast<std::size_t>(last - first), workspace_);
}

template <typename T>
void StratumSort<T>::sort(std::vector<T>& data, Workspace<T>& workspace) const {
    sortRange(data.data(), data.size(), workspace);
}

template <typename T>
void StratumSort<T>::sort(T* first, T* last, Workspace<T>& workspace) const {
    assert(first <= last);
    sortRange(first, static_cast<std::size_t>(last - first), workspace);
}

#ifdef STRATUM_ENABLE_METRICS
// ============================================================
// Research-only introspection
// ============================================================
// Runs the partitioning exactly as sort() does - same engine, same grids,
// same buffers - but records each leaf instead of sorting it. The array
// being partitioned is a copy (debugBufferA), the workspace plays its
// usual part (copied out to debugBufferB afterwards).
template <typename T>
std::vector<typename StratumSort<T>::LeafView> StratumSort<T>::debugPartitionOnly(
    const std::vector<T>& data) {
    workspace_.metrics().reset();
    const detail::Probe probe(&workspace_.metrics());

    debugA_ = data;
    std::vector<detail::LeafRecord> records;
    detail::RecordLeaves sink{&records};
    const Parameters p = resolveParameters(requested_, data.size());
    detail::sortWith</*Stable=*/false>(Traits{}, debugA_.data(), debugA_.size(),
                                       p.targetElementsPerBin, p.leafThreshold, workspace_, probe,
                                       sink);

    debugB_.assign(data.size(), T{});
    for (const detail::LeafRecord& r : records) {
        if (!r.inData) {
            const T* src = detail::WorkspaceAccess::elements(workspace_, data.size());
            std::copy(src + r.start, src + r.start + r.count, debugB_.begin() + static_cast<std::ptrdiff_t>(r.start));
        }
    }
    std::vector<LeafView> leaves;
    leaves.reserve(records.size());
    for (const detail::LeafRecord& r : records) leaves.push_back({r.inData, r.start, r.count});
    return leaves;
}
#endif

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
