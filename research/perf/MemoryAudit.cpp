// ============================================================
// MemoryAudit - auxiliary memory, measured four independent ways
// ============================================================
// The question of this study is how the auxiliary memory of a sort SCALES,
// so every number has to survive the ways a memory measurement lies:
//
//   heap        bytes requested from operator new and not yet released,
//               peak over the call, minus what was live before it
//               (AllocationTracker.hpp). Blind to the stack and to what the
//               allocator really handed out.
//   usable      the same blocks as the allocator sized them (glibc
//               malloc_usable_size): size-class rounding and headers.
//   rss         resident set: /proc/self/clear_refs resets the high-water
//               mark just before the call, VmHWM is read just after, minus
//               VmRSS before. Sees everything the process touched - heap,
//               stack, mmap - but only pages actually touched.
//   stack       the call runs on a thread whose stack was painted with a
//               pattern; the deepest overwritten byte, minus the same
//               figure for an empty function, is the stack it used.
//
// EVERY CONFIGURATION RUNS IN ITS OWN PROCESS (fork). A process that has
// already sorted keeps freed pages resident and raises glibc's mmap
// threshold, and the next measurement would reuse them and read low. A
// fresh process per row removes that.
//
// Variants: 0.10.0 (research/baselines/v0_10_0), 0.11 before this study
// (research/baselines/v0_11_pre), the current header in each memory mode
// (cur = automatic, unl = unlimited, b<bytes> = explicit budget; a trailing
// s = the stable sort), std::sort and std::stable_sort. Key types: uint8,
// uint32, uint64, float, double, and records of a uint64 key plus 8, 64 or
// 256 payload bytes.
//
//   make research-perf
//   ./build/perf_MemoryAudit [--n 1e6,1e7] [--types u64,rec72] [--shapes random,adversarial]
//                            [--variants v011,cur,unl,b0,b1m,std] > audit.csv
// Linux only (clear_refs, VmHWM, pthread stacks).
#include "AllocationTracker.hpp"
#include "BenchDatasets.hpp"

#include "stratum/StratumSort.hpp"
#include "v0_10_0/StratumSort.hpp"
#include "v0_11_pre/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace {

// ---- Records -----------------------------------------------------------
template <std::size_t P>
struct Rec {
    uint64_t key;
    unsigned char payload[P];
};
template <std::size_t P>
bool operator==(const Rec<P>& a, const Rec<P>& b) {
    return a.key == b.key && std::memcmp(a.payload, b.payload, P) == 0;
}
template <typename T>
struct IsRecord : std::false_type {};
template <std::size_t P>
struct IsRecord<Rec<P>> : std::true_type {};

// ---- Process-level probes ------------------------------------------------
long statusKB(const char* field) {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    const std::size_t len = std::strlen(field);
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, field, len) == 0) v = std::atol(line + len + 1);
    std::fclose(f);
    return v;
}
void resetHighWaterMark() {
    const int fd = open("/proc/self/clear_refs", O_WRONLY);
    if (fd >= 0) {
        if (write(fd, "5", 1) != 1) { /* not fatal: rss column reads -1 */ }
        close(fd);
    }
}

struct StackProbe {
    std::function<void()> f;
    unsigned char* low = nullptr; // lowest byte of the thread's stack
    std::size_t used = 0;
};
// Runs on the probed thread. Thread start-up and tear-down use more stack
// than a shallow function, so the region BELOW this frame is re-painted
// right before the call and scanned right after it, still inside the
// thread: only what f itself touched is counted.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void* runThunk(void* arg) {
    StackProbe& pr = *static_cast<StackProbe*>(arg);
    volatile unsigned char marker = 0;
    unsigned char* here = const_cast<unsigned char*>(&marker);
    const std::size_t margin = 512; // this frame and the call sequence
    std::memset(pr.low, 0xA5, static_cast<std::size_t>(here - pr.low) - margin);
    pr.f();
    std::size_t i = 0;
    while (pr.low + i < here && pr.low[i] == 0xA5) ++i;
    pr.used = static_cast<std::size_t>(here - (pr.low + i));
    return nullptr;
}
// Bytes of stack below the caller's frame that f touched.
std::size_t stackUsed(std::function<void()> f) {
    const std::size_t size = std::size_t{64} << 20;
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                     -1, 0);
    if (mem == MAP_FAILED) return 0;
    StackProbe pr;
    pr.f = std::move(f);
    pr.low = static_cast<unsigned char*>(mem);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, mem, size);
    pthread_t t;
    if (pthread_create(&t, &attr, runThunk, &pr) == 0) pthread_join(t, nullptr);
    pthread_attr_destroy(&attr);
    munmap(mem, size);
    return pr.used;
}

// ---- Inputs --------------------------------------------------------------
template <typename T>
std::vector<T> makeInput(const std::string& shape, std::size_t n) {
    if constexpr (IsRecord<T>::value) {
        const std::vector<uint64_t> keys = stratum::bench::makeShape<uint64_t>(shape, n, 42);
        std::vector<T> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            v[i].key = keys[i];
            std::memset(v[i].payload, static_cast<int>(i & 0xFF), sizeof v[i].payload);
            std::memcpy(v[i].payload, &i, std::min(sizeof i, sizeof v[i].payload)); // identity
        }
        return v;
    } else if constexpr (std::is_floating_point<T>::value) {
        return stratum::bench::makeFloatShape<T>(shape, n, 42);
    } else {
        return stratum::bench::makeShape<T>(shape, n, 42);
    }
}

template <typename T>
bool keyLess(const T& a, const T& b) {
    if constexpr (IsRecord<T>::value) return a.key < b.key;
    else if constexpr (std::is_floating_point<T>::value)
        return stratum::OrderedKey<T>::key(a) < stratum::OrderedKey<T>::key(b);
    else return a < b;
}
template <typename T>
bool sortedByKey(const std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i)
        if (keyLess(v[i], v[i - 1])) return false;
    return true;
}

// ---- Variants --------------------------------------------------------------
// Returns false if the variant does not apply to T. 'wsBytes' receives the
// capacity an explicit workspace holds after the call, where there is one.
template <typename T>
bool runVariant(const std::string& variant, std::vector<T>& d, std::size_t& wsBytes) {
    wsBytes = 0;
    auto byKey = [](const auto& r) { return r.key; };
    if (variant == "std") {
        std::sort(d.begin(), d.end(), [](const T& a, const T& b) { return keyLess(a, b); });
        return true;
    }
    if (variant == "stdstable") {
        std::stable_sort(d.begin(), d.end(), [](const T& a, const T& b) { return keyLess(a, b); });
        return true;
    }
    if (variant == "v010") {
        if constexpr (std::is_integral<T>::value) {
            stratum_v0_10_0::StratumSort<T>().sort(d);
            return true;
        }
        return false;
    }
    if (variant == "v011" || variant == "v011s") {
        stratum_v0_11_pre::Workspace<T> ws;
        const bool stable = variant == "v011s";
        if constexpr (IsRecord<T>::value) {
            if (stable) stratum_v0_11_pre::stable_sort_by_key(d, byKey, ws);
            else stratum_v0_11_pre::sort_by_key(d, byKey, ws);
        } else {
            if (stable) stratum_v0_11_pre::stable_sort(d, ws);
            else stratum_v0_11_pre::sort(d, ws);
        }
        wsBytes = ws.bytes();
        return true;
    }
    // The current header, by memory policy; a trailing 's' is the stable
    // sort. cur: automatic (the default); unl: kUnlimited (the partner
    // buffer at every size, the pre-study behaviour); b<bytes>[k|m]: that
    // explicit budget, e.g. b0 (the floor), b1m, b16m.
    const bool stable = !variant.empty() && variant.back() == 's' && variant != "std";
    const std::string policy = stable ? variant.substr(0, variant.size() - 1) : variant;
    std::size_t budget = 0;
    bool known = true;
    if (policy == "cur") budget = stratum::Workspace<T>::kAutomatic;
    else if (policy == "unl") budget = stratum::Workspace<T>::kUnlimited;
    else if (policy.size() > 1 && policy[0] == 'b') {
        char* end = nullptr;
        budget = static_cast<std::size_t>(std::strtod(policy.c_str() + 1, &end));
        if (*end == 'k') budget <<= 10;
        if (*end == 'm') budget <<= 20;
    } else known = false;
    if (known) {
        stratum::Workspace<T> ws(budget);
        if constexpr (IsRecord<T>::value) {
            if (stable) stratum::stable_sort_by_key(d, byKey, ws);
            else stratum::sort_by_key(d, byKey, ws);
        } else {
            if (stable) stratum::stable_sort(d, ws);
            else stratum::sort(d, ws);
        }
        wsBytes = ws.bytes();
        return true;
    }
    (void)byKey;
    return false;
}

struct Row {
    std::string variant, type, shape;
    std::size_t n = 0, elem = 0;
};

template <typename T>
void measure(const Row& r) {
    const std::vector<T> input = makeInput<T>(r.shape, r.n);
    std::vector<T> d = input;
    std::size_t wsBytes = 0;

    // 0. Warm-up on a small prefix: faults in the code pages of the variant
    //    (file-backed pages count in RSS) so the RSS window below sees data,
    //    not text. Its scratch is released before the window opens.
    {
        std::vector<T> w(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(r.n, 4096)));
        std::size_t unusedWs = 0;
        try {
            runVariant(r.variant, w, unusedWs);
        } catch (const std::length_error&) {
            // a stable sort of records refusing its budget: reported below
        }
    }

    // 1. heap, usable, rss and time, on one call.
    resetHighWaterMark();
    const long rssBefore = statusKB("VmRSS:");
    bool applies = true, refused = false;
    std::size_t heap = 0, usable = 0, allocs = 0;
    const auto t0 = std::chrono::steady_clock::now();
    {
        stratum::bench::AllocationScope scope;
        try {
            applies = runVariant(r.variant, d, wsBytes);
        } catch (const std::length_error&) {
            refused = true; // a stable sort of records under a budget below n records
        }
        heap = scope.peakBytes();
        usable = scope.peakUsableBytes();
        allocs = scope.allocations();
    }
    if (refused) {
        std::printf("%s,%s,%s,%zu,%zu,%.0f,%zu,%zu,%zu,-1,0,0,0,0,0,refused\n", r.variant.c_str(), r.type.c_str(),
                    r.shape.c_str(), r.n, sizeof(T), static_cast<double>(r.n) * sizeof(T), heap, usable, allocs);
        return;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const long rssPeak = statusKB("VmHWM:");
    if (!applies) return;
    bool ok = sortedByKey(d);
    const bool stableVariant = r.variant == "stdstable" || (r.variant != "std" && r.variant.back() == 's');
    if (ok && stableVariant && IsRecord<T>::value) {
        std::vector<T> want = input;
        std::stable_sort(want.begin(), want.end(), [](const T& a, const T& b) { return keyLess(a, b); });
        ok = d == want;
    }

    d.clear();
    d.shrink_to_fit(); // keep the largest record sizes within the machine

    // 2. stack, on a second call with a fresh copy.
    std::vector<T> d2 = input;
    std::size_t unused = 0; // (a variant that refuses was reported above)
    const std::size_t base = stackUsed([] {});
    const std::size_t raw = stackUsed([&] { runVariant(r.variant, d2, unused); });
    const std::size_t stack = raw > base ? raw - base : 0;

    const double inBytes = static_cast<double>(r.n) * static_cast<double>(sizeof(T));
    const long rssAux = (rssBefore >= 0 && rssPeak >= 0) ? (rssPeak - rssBefore) * 1024 : -1;
    std::printf("%s,%s,%s,%zu,%zu,%.0f,%zu,%zu,%zu,%ld,%zu,%zu,%.4f,%.4f,%.3f,%s\n", r.variant.c_str(),
                r.type.c_str(), r.shape.c_str(), r.n, sizeof(T), inBytes, heap, usable, allocs, rssAux,
                stack, wsBytes, heap / inBytes, rssAux / inBytes, ms, ok ? "ok" : "WRONG");
    std::fflush(stdout);
}

void measureRow(const Row& r) {
    if (r.type == "u8") measure<uint8_t>(r);
    else if (r.type == "u32") measure<uint32_t>(r);
    else if (r.type == "u64") measure<uint64_t>(r);
    else if (r.type == "f32") measure<float>(r);
    else if (r.type == "f64") measure<double>(r);
    else if (r.type == "rec16") measure<Rec<8>>(r);
    else if (r.type == "rec72") measure<Rec<64>>(r);
    else if (r.type == "rec264") measure<Rec<256>>(r);
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> sizes = {"1e4", "1e5", "1e6", "1e7"};
    std::vector<std::string> types = {"u8", "u32", "u64", "f32", "f64", "rec16", "rec72", "rec264"};
    std::vector<std::string> shapes = {"random"};
    std::vector<std::string> variants = {"v010", "v011", "v011s", "std", "stdstable"};
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--n") sizes = split(v);
        else if (k == "--types") types = split(v);
        else if (k == "--shapes") shapes = split(v);
        else if (k == "--variants") variants = split(v);
    }
    std::printf("variant,type,shape,n,elem_bytes,input_bytes,heap_peak,usable_peak,allocations,rss_aux,"
                "stack_bytes,workspace_bytes,heap_per_input,rss_per_input,ms,ok\n");
    std::fflush(stdout);
    for (const auto& ns : sizes) {
        const std::size_t n = static_cast<std::size_t>(std::strtod(ns.c_str(), nullptr));
        for (const auto& t : types)
            for (const auto& sh : shapes)
                for (const auto& v : variants) {
                    const pid_t pid = fork();
                    if (pid == 0) {
                        measureRow({v, t, sh, n, 0});
                        std::fflush(stdout);
                        _exit(0);
                    }
                    int status = 0;
                    waitpid(pid, &status, 0);
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        std::printf("%s,%s,%s,%zu,CHILD FAILED status=%d\n", v.c_str(), t.c_str(), sh.c_str(), n,
                                    status);
                        std::fflush(stdout); // or every later child prints it again
                    }
                }
    }
    return 0;
}
