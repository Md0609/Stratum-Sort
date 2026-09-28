#pragma once

// ============================================================
// Workspace - the scratch memory of one sort, separated from its settings
// ============================================================
// Up to 0.10.0 the scratch buffers were members of StratumSort, so a
// sorter was simultaneously a CONFIGURATION (lambda, t) and a piece of
// MUTABLE STATE. That is why one instance could not be shared between
// threads: two calls would write the same buffers.
//
// A Workspace is only the state. A StratumSort configured once can sort
// on as many threads as there are workspaces:
//
//     const stratum::StratumSort<int64_t> sorter;         // settings only
//     // thread 1                       // thread 2
//     stratum::Workspace<int64_t> w1;   stratum::Workspace<int64_t> w2;
//     sorter.sort(a, w1);               sorter.sort(b, w2);
//
// No lock is involved, because nothing is shared: sort(data, workspace) is
// const and touches no member of the sorter.
//
// WHAT IT HOLDS, and why this is the whole auxiliary memory of a sort:
//   - one buffer of n elements, the partner of the caller's array in the
//     ping-pong between refinement levels;
//   - one array of 2 * ceil(n / lambda) + 2 counters, 32-bit whenever n
//     fits in 32 bits, for the per-bucket histograms of every level.
// It grows on demand and never shrinks by itself; release() gives the
// memory back. Reusing one workspace across calls is what avoids paying
// for the allocation - and, for a large n, the page faults of touching it
// for the first time - on every call.
//
// A workspace is not tied to a sorter or to a size: any sorter may use any
// workspace of the right element type, for any n. It is movable and not
// copyable. Using ONE workspace from two threads at once is a data race,
// exactly as using one std::vector from two threads would be.
// ============================================================

#include "Config.hpp"

#ifdef STRATUM_ENABLE_METRICS
#include "Metrics.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

namespace detail {
struct WorkspaceAccess;
} // namespace detail

template <typename T>
class Workspace {
    static_assert(std::is_trivially_copyable<T>::value,
                  "Stratum Sort moves elements with memcpy: the element type must be "
                  "trivially copyable");
    static_assert(std::is_default_constructible<T>::value,
                  "the scratch buffer is an array of T, so T must be default constructible");

public:
    Workspace() = default;
    Workspace(Workspace&&) noexcept = default;
    Workspace& operator=(Workspace&&) noexcept = default;
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    // Bytes currently held, all buffers included.
    std::size_t bytes() const {
        return elementCapacity_ * sizeof(T) + count32Capacity_ * sizeof(uint32_t) +
               count64Capacity_ * sizeof(uint64_t);
    }

    // Frees everything. The next sort allocates again.
    void release() noexcept {
        elements_.reset();
        elementCapacity_ = 0;
        counts32_.reset();
        count32Capacity_ = 0;
        counts64_.reset();
        count64Capacity_ = 0;
    }

#ifdef STRATUM_ENABLE_METRICS
    // Research build only: statistics of the last sort that used this
    // workspace.
    const SortMetrics& metrics() const { return metrics_; }
    SortMetrics& metrics() { return metrics_; }
#endif

private:
    friend struct detail::WorkspaceAccess;

    // Grow-only. The old block is released BEFORE the new one is
    // requested, so growing never holds both: the peak is the new size,
    // not old + new. If the request throws, the workspace is left empty
    // and valid, and - because the engine reserves everything before its
    // first write - the caller's array is untouched.
    T* reserveElements(std::size_t n) {
        if (n > elementCapacity_) {
            elements_.reset();
            elementCapacity_ = 0;
            elements_.reset(new T[n]); // default-initialised: no zero fill
            elementCapacity_ = n;
        }
        return elements_.get();
    }

    uint32_t* reserveCounts(std::size_t k, uint32_t*) {
        if (k > count32Capacity_) {
            counts32_.reset();
            count32Capacity_ = 0;
            counts32_.reset(new uint32_t[k]);
            count32Capacity_ = k;
        }
        return counts32_.get();
    }

    uint64_t* reserveCounts(std::size_t k, uint64_t*) {
        if (k > count64Capacity_) {
            counts64_.reset();
            count64Capacity_ = 0;
            counts64_.reset(new uint64_t[k]);
            count64Capacity_ = k;
        }
        return counts64_.get();
    }

    std::unique_ptr<T[]> elements_;
    std::size_t elementCapacity_ = 0;
    std::unique_ptr<uint32_t[]> counts32_;
    std::size_t count32Capacity_ = 0;
    std::unique_ptr<uint64_t[]> counts64_;
    std::size_t count64Capacity_ = 0;

#ifdef STRATUM_ENABLE_METRICS
    SortMetrics metrics_;
#endif
};

namespace detail {
// The engine's only door into a workspace. Keeps reserve*() out of the
// public interface without making every engine instantiation a friend.
struct WorkspaceAccess {
    template <typename T>
    static T* elements(Workspace<T>& w, std::size_t n) {
        return w.reserveElements(n);
    }
    template <typename T, typename Count>
    static Count* counts(Workspace<T>& w, std::size_t k) {
        return w.reserveCounts(k, static_cast<Count*>(nullptr));
    }
};
} // namespace detail

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
