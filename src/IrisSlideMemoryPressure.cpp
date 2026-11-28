//
//  IrisSlideMemoryPressure.cpp
//  Iris
//
//  D26: each operating system's own memory-pressure signal, read the cheapest way it offers. The
//  thresholds on Linux and Windows are where those systems' numbers are commonly taken to mean
//  pressure *(to tune on loaded machines)*; macOS states its levels itself.
//
#include "IrisSlideMemoryPressure.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace IrisSlide {
namespace {
#if defined(__APPLE__)
// xnu's levels, as libdispatch's DISPATCH_MEMORYPRESSURE_WARN and _CRITICAL name them.
constexpr int MACOS_WARN = 2, MACOS_CRITICAL = 4;
#elif defined(_WIN32)
// GlobalMemoryStatusEx's dwMemoryLoad, the percentage of physical memory in use.
constexpr unsigned long WINDOWS_WARNING_LOAD = 90, WINDOWS_CRITICAL_LOAD = 97;
#elif defined(__linux__)
// Pressure stall information (kernel Documentation/accounting/psi.rst): the share of the last
// 10 s in which some task (some) or every task (full) stalled waiting for memory.
constexpr double LINUX_SOME_WARNING = 10.0, LINUX_FULL_CRITICAL = 10.0;
// Without it, MemAvailable as a share of MemTotal.
constexpr double LINUX_AVAILABLE_WARNING = 0.10, LINUX_AVAILABLE_CRITICAL = 0.05;

/// avg10 of the /proc/pressure/memory line that starts with @p kind, or a negative value.
double stall_share (const std::string& text, const std::string& kind)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind(kind + " ", 0) != 0) continue;
        const auto at = line.find("avg10=");
        if (at == std::string::npos) return -1;
        return std::strtod(line.c_str() + at + 6, nullptr);   // strtod reports nothing it cannot read as 0
    }
    return -1;
}
/// The kB value of /proc/meminfo's @p field, or 0.
double meminfo_kb (const std::string& text, const std::string& field)
{
    const auto at = text.find(field + ":");
    if (at == std::string::npos) return 0;
    return std::strtod(text.c_str() + at + field.size() + 1, nullptr);
}
std::string whole_file (const char* path)
{
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}
#endif
} // namespace

// RTM Encode_11
MemoryPressure memory_pressure () noexcept
{
#if defined(__APPLE__)
    int level = 0;
    std::size_t length = sizeof(level);
    const bool is_read = sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &length, nullptr, 0) == 0;
    if (!is_read) return MemoryPressure::NORMAL;
    if (level >= MACOS_CRITICAL) return MemoryPressure::CRITICAL;
    if (level >= MACOS_WARN) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
#elif defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    const bool is_read = GlobalMemoryStatusEx(&status) != 0;
    if (!is_read) return MemoryPressure::NORMAL;
    if (status.dwMemoryLoad >= WINDOWS_CRITICAL_LOAD) return MemoryPressure::CRITICAL;
    if (status.dwMemoryLoad >= WINDOWS_WARNING_LOAD) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
#elif defined(__linux__)
    const std::string psi = whole_file("/proc/pressure/memory");
    if (!psi.empty()) {
        if (stall_share(psi, "full") >= LINUX_FULL_CRITICAL) return MemoryPressure::CRITICAL;
        if (stall_share(psi, "some") >= LINUX_SOME_WARNING) return MemoryPressure::WARNING;
        return MemoryPressure::NORMAL;
    }
    const std::string meminfo = whole_file("/proc/meminfo");
    const double total = meminfo_kb(meminfo, "MemTotal"), available = meminfo_kb(meminfo, "MemAvailable");
    if (total <= 0) return MemoryPressure::NORMAL;
    if (available / total <= LINUX_AVAILABLE_CRITICAL) return MemoryPressure::CRITICAL;
    if (available / total <= LINUX_AVAILABLE_WARNING) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
#else
    return MemoryPressure::NORMAL;   // no signal here (the browser module, other systems)
#endif
}
} // namespace IrisSlide
