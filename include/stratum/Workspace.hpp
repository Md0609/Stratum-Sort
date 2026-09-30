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
// WHAT IT HOLDS - the whole auxiliary memory of a sort - is at most one
// buffer of elements and one array of counters (32-bit whenever n fits in
// 32 bits), sized by the strategy the budget below selects: n elements and
// 2 * ceil(n / lambda) + 2 counters for the partner-buffer strategy, or a
// counter arena set by the key width, block buffers and a partner buffer
// of what is left for the in-place one. It grows on demand and never
// shrinks by itself (unless a smaller budget is set); release() gives the
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

#include <algorithm>
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
    // MEMORY BUDGET. Everything a sort holds besides the caller's array
    // lives in its workspace, and the workspace can bound it:
    //
    //   kUnlimited (the default)  always the partner buffer, n elements -
    //                             the fastest strategy on most machines;
    //   kAutomatic                the partner buffer while it needs at most
    //                             AUTOMATIC_MEMORY_LIMIT (16 MiB), and that
    //                             many bytes at most above it (Config.hpp);
    //   any number of bytes       the sort changes strategy to stay within
    //                             it: in place by blocks, then by the
    //                             American flag permutation as the budget
    //                             shrinks (research/ALGORITHM.md section 14).
    //
    // The floor is the in-place engine's counters, 44 KB for 64-bit keys,
    // set by the key width and never by n: a smaller budget is raised to it. The
    // one sort that cannot honour a budget is the stable sort of records
    // (stable_sort_by_key), whose linear-time strategy needs n records of
    // scratch: given an explicit budget below that, it throws
    // std::length_error before touching the input. Under kAutomatic it takes
    // what it needs.
    static constexpr std::size_t kUnlimited = static_cast<std::size_t>(-1);
    static constexpr std::size_t kAutomatic = static_cast<std::size_t>(-2);

    Workspace() = default;
    explicit Workspace(std::size_t budgetBytes) : budget_(budgetBytes) {}
    std::size_t budget() const { return budget_; }
    void setBudget(std::size_t budgetBytes) {
        budget_ = budgetBytes;
        if (budget_ < kAutomatic && bytes() > budget_) release();
    }

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

    std::size_t budget_ = kUnlimited;
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
    template <typename T>
    static std::size_t budget(const Workspace<T>& w) {
        return w.budget_;
    }
    template <typename T>
    static void releaseElements(Workspace<T>& w) {
        w.elements_.reset();
        w.elementCapacity_ = 0;
    }
    // The bytes w would hold after reserving 'elements' elements and
    // 'counts' counters of type Count, reusing what it already holds.
    template <typename T, typename Count>
    static std::size_t bytesAfter(const Workspace<T>& w, std::size_t elements, std::size_t counts) {
        const bool is32 = std::is_same<Count, uint32_t>::value;
        const std::size_t c32 = is32 ? std::max(w.count32Capacity_, counts) : w.count32Capacity_;
        const std::size_t c64 = is32 ? w.count64Capacity_ : std::max(w.count64Capacity_, counts);
        return std::max(w.elementCapacity_, elements) * sizeof(T) + c32 * sizeof(uint32_t) +
               c64 * sizeof(uint64_t);
    }
    // Makes room for that reservation within 'budget' bytes: keeps what w
    // holds if the total fits, releases everything otherwise. A reservation
    // that needs at most 'budget' bytes then never leaves w above it.
    template <typename T, typename Count>
    static void fit(Workspace<T>& w, std::size_t elements, std::size_t counts, std::size_t budget) {
        if (bytesAfter<T, Count>(w, elements, counts) > budget) w.release();
    }
};
} // namespace detail

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
