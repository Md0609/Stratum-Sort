#pragma once

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif
#endif

// STRATUM_CXXFLAGS is injected by the build (-DSTRATUM_CXXFLAGS="\"...\"") so the
// binary can report exactly the flags it was built with, instead of
// guessing. If the project is compiled without going through the Makefile
// or CMake, fall back to a placeholder rather than fail to build.
#ifndef STRATUM_CXXFLAGS
#define STRATUM_CXXFLAGS "unknown (built without the project Makefile or CMake)"
#endif
#ifndef STRATUM_BUILD_CONFIG
#define STRATUM_BUILD_CONFIG "unknown"
#endif

namespace stratum {

// ============================================================
// SystemInfo
// ============================================================
// Best-effort collection of compiler, build and machine information to
// attach to benchmark reports, so results can be traced back to exactly
// how and where they were produced. Every field falls back to a clearly
// labeled "unknown" value rather than guessing when it cannot be
// determined - this is meant to be trustworthy, not exhaustive.
//
// Nothing here is used by the library. Cache sizes in particular are
// REPORTED, never consulted: the library's automatic parameters are a
// fixed function of n and the key width, so that the same input produces
// the same partition on every machine (see Config.hpp).
// ============================================================
struct SystemInfo {
    std::string compilerName;
    std::string compilerVersion;
    std::string languageStandard;
    std::string compileFlags;
    std::string buildConfig;
    std::string architecture;
    std::string operatingSystem;
    std::string cpuModel;
    std::string caches;
    unsigned logicalCores = 0;

    static SystemInfo collect() {
        SystemInfo info;

        // ---- Compiler name and version -----------------------------------
#if defined(__clang__)
        info.compilerName = "Clang";
        info.compilerVersion = __clang_version__;
#elif defined(__GNUC__)
        info.compilerName = "GCC";
        {
            std::ostringstream oss;
            oss << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
            info.compilerVersion = oss.str();
        }
#elif defined(_MSC_VER)
        info.compilerName = "MSVC";
        info.compilerVersion = std::to_string(_MSC_FULL_VER);
#else
        info.compilerName = "unknown";
        info.compilerVersion = "unknown";
#endif

        // ---- Language standard ---------------------------------------------
        // MSVC reports 199711L in __cplusplus unless /Zc:__cplusplus is given;
        // _MSVC_LANG is its reliable spelling.
#if defined(_MSVC_LANG)
        info.languageStandard = "C++ " + std::to_string(_MSVC_LANG);
#else
        info.languageStandard = "C++ " + std::to_string(__cplusplus);
#endif

        info.compileFlags = STRATUM_CXXFLAGS;
        info.buildConfig = STRATUM_BUILD_CONFIG;

        // ---- Architecture ---------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
        info.architecture = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
        info.architecture = "x86 (32-bit)";
#elif defined(__aarch64__) || defined(_M_ARM64)
        info.architecture = "arm64";
#elif defined(__arm__)
        info.architecture = "arm (32-bit)";
#else
        info.architecture = "unknown";
#endif

        // ---- Operating system ------------------------------------------------
#if defined(__linux__)
        info.operatingSystem = "Linux";
#elif defined(__APPLE__)
        info.operatingSystem = "macOS";
#elif defined(_WIN32)
        info.operatingSystem = "Windows";
#elif defined(__unix__)
        info.operatingSystem = "Unix";
#else
        info.operatingSystem = "unknown";
#endif

        info.cpuModel = readCpuModel();
        info.caches = readCaches();
        info.logicalCores = std::thread::hardware_concurrency();
        return info;
    }

    void print(std::ostream& os) const {
        os << "  Compiler:        " << compilerName << " " << compilerVersion << "\n";
        os << "  Standard:        " << languageStandard << "\n";
        os << "  Build config:    " << buildConfig << "\n";
        os << "  Compile flags:   " << compileFlags << "\n";
        os << "  Architecture:    " << architecture << "\n";
        os << "  Operating system:" << operatingSystem << "\n";
        os << "  CPU:             " << cpuModel << "\n";
        os << "  Caches:          " << caches << "\n";
        os << "  Logical cores:   " << logicalCores << "\n";
    }

private:
    static std::string humanBytes(unsigned long long b) {
        std::ostringstream oss;
        if (b >= (1ull << 20) && b % (1ull << 20) == 0) oss << (b >> 20) << " MiB";
        else if (b >= 1024 && b % 1024 == 0) oss << (b >> 10) << " KiB";
        else oss << b << " B";
        return oss.str();
    }

    static std::string readCpuModel() {
#if defined(__APPLE__)
        char buffer[256];
        std::size_t size = sizeof(buffer);
        if (::sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0) {
            return std::string(buffer, size > 0 ? size - 1 : 0);
        }
        return "unknown";
#elif defined(__linux__)
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (!cpuinfo.is_open()) return "unknown";
        std::string line;
        while (std::getline(cpuinfo, line)) {
            for (const std::string key : {"model name", "Model", "Hardware"}) {
                if (line.compare(0, key.size(), key) == 0) {
                    const auto colon = line.find(':');
                    if (colon != std::string::npos && colon + 2 <= line.size())
                        return line.substr(colon + 2);
                }
            }
        }
        return "unknown";
#elif defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
        int regs[4] = {0, 0, 0, 0};
        char brand[49] = {0};
        __cpuid(regs, static_cast<int>(0x80000000));
        if (static_cast<unsigned>(regs[0]) < 0x80000004u) return "unknown";
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, static_cast<int>(0x80000002 + i));
            std::memcpy(brand + 16 * i, regs, 16);
        }
        std::string s(brand);
        const auto first = s.find_first_not_of(' ');
        return first == std::string::npos ? "unknown" : s.substr(first);
#else
        return "unknown";
#endif
    }

    static std::string readCaches() {
#if defined(__linux__)
        std::string out;
        for (int idx = 0; idx < 8; ++idx) {
            const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(idx) + "/";
            std::ifstream level(base + "level"), type(base + "type"), size(base + "size");
            if (!level.is_open() || !type.is_open() || !size.is_open()) continue;
            std::string l, t, s;
            std::getline(level, l);
            std::getline(type, t);
            std::getline(size, s);
            if (t == "Instruction") continue;
            if (!out.empty()) out += ", ";
            out += "L" + l + (t == "Data" ? "d" : "") + " " + s;
        }
        return out.empty() ? "unknown" : out;
#elif defined(__APPLE__)
        std::string out;
        auto get = [](const char* name) -> unsigned long long {
            unsigned long long v = 0;
            std::size_t sz = sizeof(v);
            if (::sysctlbyname(name, &v, &sz, nullptr, 0) != 0) return 0;
            return v;
        };
        const unsigned long long l1 = get("hw.perflevel0.l1dcachesize") ? get("hw.perflevel0.l1dcachesize") : get("hw.l1dcachesize");
        const unsigned long long l2 = get("hw.perflevel0.l2cachesize") ? get("hw.perflevel0.l2cachesize") : get("hw.l2cachesize");
        const unsigned long long l3 = get("hw.l3cachesize");
        if (l1) out += "L1d " + humanBytes(l1);
        if (l2) out += std::string(out.empty() ? "" : ", ") + "L2 " + humanBytes(l2);
        if (l3) out += std::string(out.empty() ? "" : ", ") + "L3 " + humanBytes(l3);
        return out.empty() ? "unknown" : out;
#elif defined(_WIN32)
        DWORD len = 0;
        GetLogicalProcessorInformation(nullptr, &len);
        if (len == 0) return "unknown";
        // A vector of the struct itself, so the buffer is correctly aligned.
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
            len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = buffer.data();
        if (!GetLogicalProcessorInformation(info, &len)) return "unknown";
        unsigned long long l1 = 0, l2 = 0, l3 = 0;
        const std::size_t count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (std::size_t i = 0; i < count; ++i) {
            if (info[i].Relationship != RelationCache) continue;
            const CACHE_DESCRIPTOR& c = info[i].Cache;
            if (c.Type == CacheInstruction) continue;
            if (c.Level == 1 && !l1) l1 = c.Size;
            if (c.Level == 2 && !l2) l2 = c.Size;
            if (c.Level == 3 && !l3) l3 = c.Size;
        }
        std::string out;
        if (l1) out += "L1d " + humanBytes(l1);
        if (l2) out += std::string(out.empty() ? "" : ", ") + "L2 " + humanBytes(l2);
        if (l3) out += std::string(out.empty() ? "" : ", ") + "L3 " + humanBytes(l3);
        return out.empty() ? "unknown" : out;
#else
        return "unknown";
#endif
    }
};

} // namespace stratum
