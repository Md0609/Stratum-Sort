#pragma once

#include <fstream>
#include <sstream>
#include <string>

// DRS_CXXFLAGS is injected by the Makefile (-DDRS_CXXFLAGS="\"...\"") so the
// binary can report exactly the flags it was built with, instead of
// guessing. If the project is compiled without going through the Makefile,
// fall back to a placeholder rather than fail to build.
#ifndef DRS_CXXFLAGS
#define DRS_CXXFLAGS "unknown (built without the project Makefile)"
#endif

namespace drs {

// ============================================================
// SystemInfo
// ============================================================
// Best-effort collection of compiler, build and machine information to
// attach to benchmark reports, so results can be traced back to exactly
// how and where they were produced. Every field falls back to a clearly
// labeled "unknown" value rather than guessing when it cannot be
// determined - this is meant to be trustworthy, not exhaustive.
// ============================================================
struct SystemInfo {
    std::string compilerName;
    std::string compilerVersion;
    std::string compileFlags;
    std::string architecture;
    std::string operatingSystem;
    std::string cpuModel;

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
        info.compilerVersion = std::to_string(_MSC_VER);
#else
        info.compilerName = "unknown";
        info.compilerVersion = "unknown";
#endif

        // ---- Compile flags (injected by the Makefile) ----------------------
        info.compileFlags = DRS_CXXFLAGS;

        // ---- Architecture ---------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
        info.architecture = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
        info.architecture = "x86 (32-bit)";
#elif defined(__aarch64__)
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

        // ---- CPU model (best effort, Linux only) ------------------------------
        info.cpuModel = readCpuModelLinux();

        return info;
    }

    void print(std::ostream& os) const {
        os << "  Compiler:        " << compilerName << " " << compilerVersion << "\n";
        os << "  Compile flags:   " << compileFlags << "\n";
        os << "  Architecture:    " << architecture << "\n";
        os << "  Operating system:" << operatingSystem << "\n";
        os << "  CPU:             " << cpuModel << "\n";
    }

private:
    static std::string readCpuModelLinux() {
#if defined(__linux__)
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (!cpuinfo.is_open()) return "unknown";

        std::string line;
        while (std::getline(cpuinfo, line)) {
            const std::string key = "model name";
            if (line.compare(0, key.size(), key) == 0) {
                const auto colon = line.find(':');
                if (colon != std::string::npos && colon + 2 <= line.size()) {
                    return line.substr(colon + 2);
                }
            }
        }
        return "unknown";
#else
        return "unknown";
#endif
    }
};

} // namespace drs
