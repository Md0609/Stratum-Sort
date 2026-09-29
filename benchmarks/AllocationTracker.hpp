#pragma once

// ============================================================
// AllocationTracker - peak auxiliary memory, measured at the allocator
// ============================================================
// Replaces the global operator new/delete so that every byte a call
// allocates is counted, whoever allocates it: the sorter, its scratch
// buffers, or anything the standard library does on its behalf. That is
// the only honest way to compare the memory of two implementations - a
// formula in a comment is a claim, the allocator is a measurement.
//
// Usage, around the call being measured:
//
//     stratum::bench::AllocationScope scope;   // snapshots live bytes
//     sorter.sort(data);
//     scope.peakBytes();      // highest live total above the snapshot
//     scope.retainedBytes();  // still live when the scope is read
//
// "Peak" is the maximum, over the duration of the scope, of the bytes
// allocated and not yet released, minus what was live when the scope
// began. It therefore excludes the input itself (allocated earlier) and
// includes scratch an instance keeps between calls.
//
// REPLACEMENT FUNCTIONS CANNOT BE INLINE. Include this header in exactly
// one translation unit of a program. Every benchmark here is a single
// translation unit, which is why it is a header at all.
//
// Single-threaded by design: the counters are plain integers. The
// benchmarks never allocate from two threads at once while measuring.
// ============================================================

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#if defined(__GLIBC__) || defined(__linux__)
#include <malloc.h>
#define STRATUM_BENCH_USABLE(p) malloc_usable_size(p)
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#define STRATUM_BENCH_USABLE(p) malloc_size(p)
#elif defined(_WIN32)
#include <malloc.h>
#define STRATUM_BENCH_USABLE(p) _msize(p)
#endif

namespace stratum {
namespace bench {

struct AllocationCounters {
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t allocations = 0;
    // What the allocator really handed out for the same blocks - requested
    // size rounded up to its size classes, plus this tracker's own header -
    // where the platform can say (glibc: malloc_usable_size). Equal to the
    // requested figures elsewhere.
    std::size_t liveUsable = 0;
    std::size_t peakUsable = 0;
};

inline AllocationCounters& allocationCounters() {
    static AllocationCounters counters;
    return counters;
}

class AllocationScope {
public:
    AllocationScope() {
        AllocationCounters& c = allocationCounters();
        baseLive_ = c.live;
        baseAllocations_ = c.allocations;
        c.peak = c.live;
        baseUsable_ = c.liveUsable;
        c.peakUsable = c.liveUsable;
    }
    std::size_t peakBytes() const { return allocationCounters().peak - baseLive_; }
    std::size_t peakUsableBytes() const { return allocationCounters().peakUsable - baseUsable_; }
    std::size_t retainedBytes() const {
        const std::size_t live = allocationCounters().live;
        return live > baseLive_ ? live - baseLive_ : 0;
    }
    std::size_t allocations() const { return allocationCounters().allocations - baseAllocations_; }

private:
    std::size_t baseLive_ = 0;
    std::size_t baseAllocations_ = 0;
    std::size_t baseUsable_ = 0;
};

namespace detail {

// Every block carries its size in a header. The header is as large as the
// strictest fundamental alignment, so the pointer handed out keeps the
// alignment malloc gave the block.
constexpr std::size_t kHeader = alignof(std::max_align_t) > sizeof(std::size_t)
                                    ? alignof(std::max_align_t)
                                    : sizeof(std::size_t);

inline void* trackedAllocate(std::size_t sz) {
    if (sz == 0) sz = 1;
    void* raw = std::malloc(sz + kHeader);
    if (!raw) return nullptr;
    *static_cast<std::size_t*>(raw) = sz;
    AllocationCounters& c = allocationCounters();
    c.live += sz;
    ++c.allocations;
    if (c.live > c.peak) c.peak = c.live;
#ifdef STRATUM_BENCH_USABLE
    c.liveUsable += STRATUM_BENCH_USABLE(raw);
#else
    c.liveUsable += sz + kHeader;
#endif
    if (c.liveUsable > c.peakUsable) c.peakUsable = c.liveUsable;
    return static_cast<char*>(raw) + kHeader;
}

inline void trackedRelease(void* p) noexcept {
    if (!p) return;
    // Through uintptr_t, not char*: once GCC inlines this into a delete of a
    // T[] it "sees" a negative subscript of that array and warns, although
    // the block really starts kHeader bytes earlier - it was allocated so.
    char* raw = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(p) - kHeader);
    AllocationCounters& c = allocationCounters();
    const std::size_t sz = *reinterpret_cast<std::size_t*>(raw);
    c.live -= sz;
#ifdef STRATUM_BENCH_USABLE
    c.liveUsable -= STRATUM_BENCH_USABLE(raw);
#else
    c.liveUsable -= sz + kHeader;
#endif
    std::free(raw);
}

} // namespace detail
} // namespace bench
} // namespace stratum

void* operator new(std::size_t sz) {
    void* p = stratum::bench::detail::trackedAllocate(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) { return operator new(sz); }
void* operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    return stratum::bench::detail::trackedAllocate(sz);
}
void* operator new[](std::size_t sz, const std::nothrow_t&) noexcept {
    return stratum::bench::detail::trackedAllocate(sz);
}

// Same false positive as in tests/api_contract.cpp: a consistent global
// replacement pair is exactly what the standard permits.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { stratum::bench::detail::trackedRelease(p); }
void operator delete[](void* p) noexcept { stratum::bench::detail::trackedRelease(p); }
void operator delete(void* p, std::size_t) noexcept { stratum::bench::detail::trackedRelease(p); }
void operator delete[](void* p, std::size_t) noexcept { stratum::bench::detail::trackedRelease(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept {
    stratum::bench::detail::trackedRelease(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    stratum::bench::detail::trackedRelease(p);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
