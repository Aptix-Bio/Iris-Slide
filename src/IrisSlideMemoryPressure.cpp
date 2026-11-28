//
//  IrisSlideMemoryPressure.cpp
//  Iris
//
//  D26: each operating system's own memory-pressure signal, read the cheapest way it offers. The
//  thresholds on Linux and Windows are where those systems' numbers are commonly taken to mean
//  pressure *(to tune on loaded machines)*; macOS states its levels itself.
//
#include "IrisSlideMemoryPressure.hpp"

#include <array>
#include <cstdlib>
#include <optional>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
// <windows.h> defines min and max as macros unless NOMINMAX is set first, and they break
// std::min. Iris-Headers' IrisTypes.hpp sets it, but this file includes no Iris header.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace IrisSlide {
namespace {
// xnu's levels, as libdispatch's DISPATCH_MEMORYPRESSURE_WARN and _CRITICAL name them.
constexpr int MACOS_WARN = 2, MACOS_CRITICAL = 4;
// GlobalMemoryStatusEx's dwMemoryLoad, the percentage of physical memory in use.
constexpr unsigned long WINDOWS_WARNING_LOAD = 90, WINDOWS_CRITICAL_LOAD = 97;
// Pressure stall information (kernel Documentation/accounting/psi.rst): the share of the last
// 10 s in which some task (some) or every task (full) stalled waiting for memory.
constexpr double LINUX_SOME_WARNING = 10.0, LINUX_FULL_CRITICAL = 10.0;
// Without it, MemAvailable as a share of MemTotal.
constexpr double LINUX_AVAILABLE_WARNING = 0.10, LINUX_AVAILABLE_CRITICAL = 0.05;
constexpr std::string_view AVG10 = "avg10=";

/// The longest number leading_number reads; a pressure share or a kB count is far shorter.
constexpr std::size_t NUMBER_CHARS = 31;

/// The number that opens @p text after any spaces, or nothing when none does. Read with strtod
/// from a terminated copy, since Apple's libc++ for macOS 15 deletes std::from_chars for double.
std::optional<double> leading_number (std::string_view text) noexcept
{
    const auto start = text.find_first_not_of(' ');
    if (start == std::string_view::npos) return std::nullopt;
    std::array<char, NUMBER_CHARS + 1> digits {};
    const auto length = std::min(NUMBER_CHARS, text.size() - start);
    text.copy(digits.data(), length, start);
    char* end = nullptr;
    const double value = std::strtod(digits.data(), &end);
    if (end == digits.data()) return std::nullopt;
    return value;
}
/// avg10 of the /proc/pressure/memory line that starts with @p kind, or nothing.
std::optional<double> stall_share (std::string_view psi, std::string_view kind) noexcept
{
    while (!psi.empty()) {
        const auto end = psi.find('\n');
        const auto line = psi.substr(0, end);
        psi = end == std::string_view::npos ? std::string_view{} : psi.substr(end + 1);
        if (!line.starts_with(kind) || line.size() <= kind.size() || line[kind.size()] != ' ') continue;
        const auto at = line.find(AVG10);
        if (at == std::string_view::npos) return std::nullopt;
        return leading_number(line.substr(at + AVG10.size()));
    }
    return std::nullopt;
}
/// The kB value of /proc/meminfo's @p field, or nothing.
std::optional<double> meminfo_kb (std::string_view meminfo, std::string_view field) noexcept
{
    for (std::size_t at = meminfo.find(field); at != std::string_view::npos; at = meminfo.find(field, at + 1)) {
        const bool starts_line = at == 0 || meminfo[at - 1] == '\n';
        const auto after = at + field.size();
        if (starts_line && after < meminfo.size() && meminfo[after] == ':') return leading_number(meminfo.substr(after + 1));
    }
    return std::nullopt;
}
#if defined(__linux__)
/// /proc/meminfo is about 1.5 KB and /proc/pressure/memory under 200 bytes.
constexpr std::size_t PROC_FILE_BYTES = 8192;

/// A file descriptor, closed when it leaves scope; negative when the open failed.
class FileDescriptor {
public:
    explicit FileDescriptor (const char* path) noexcept : _fd(::open(path, O_RDONLY | O_CLOEXEC)) {}
    FileDescriptor (const FileDescriptor&) = delete;
    FileDescriptor& operator= (const FileDescriptor&) = delete;
    ~FileDescriptor () { if (_fd >= 0) ::close(_fd); }
    [[nodiscard]] bool is_open () const noexcept { return _fd >= 0; }
    [[nodiscard]] int get () const noexcept { return _fd; }
private:
    int _fd;
};

/// The start of the file at @p path, read into @p buffer with no allocation; empty when it cannot be read.
std::string_view proc_file (const char* path, std::array<char, PROC_FILE_BYTES>& buffer) noexcept
{
    const FileDescriptor file(path);
    if (!file.is_open()) return {};
    std::size_t length = 0;
    while (length < buffer.size()) {
        const ssize_t got = ::read(file.get(), buffer.data() + length, buffer.size() - length);
        if (got <= 0) break;
        length += static_cast<std::size_t>(got);
    }
    return {buffer.data(), length};
}
#endif
} // namespace

MemoryPressure pressure_of_macos_level (int level) noexcept
{
    if (level >= MACOS_CRITICAL) return MemoryPressure::CRITICAL;
    if (level >= MACOS_WARN) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
}

MemoryPressure pressure_of_windows_load (unsigned long load) noexcept
{
    if (load >= WINDOWS_CRITICAL_LOAD) return MemoryPressure::CRITICAL;
    if (load >= WINDOWS_WARNING_LOAD) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
}

MemoryPressure pressure_of_linux_psi (std::string_view psi) noexcept
{
    const auto full = stall_share(psi, "full");
    if (full && *full >= LINUX_FULL_CRITICAL) return MemoryPressure::CRITICAL;
    const auto some = stall_share(psi, "some");
    if (some && *some >= LINUX_SOME_WARNING) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
}

MemoryPressure pressure_of_linux_meminfo (std::string_view meminfo) noexcept
{
    const auto total = meminfo_kb(meminfo, "MemTotal"), available = meminfo_kb(meminfo, "MemAvailable");
    if (!total || !available || *total <= 0) return MemoryPressure::NORMAL;
    const double share = *available / *total;
    if (share <= LINUX_AVAILABLE_CRITICAL) return MemoryPressure::CRITICAL;
    if (share <= LINUX_AVAILABLE_WARNING) return MemoryPressure::WARNING;
    return MemoryPressure::NORMAL;
}

// RTM Encode_11
MemoryPressure memory_pressure () noexcept
{
#if defined(__APPLE__)
    int level = 0;
    std::size_t length = sizeof(level);
    const bool is_read = sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &length, nullptr, 0) == 0;
    return is_read ? pressure_of_macos_level(level) : MemoryPressure::NORMAL;
#elif defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    const bool is_read = GlobalMemoryStatusEx(&status) != 0;
    return is_read ? pressure_of_windows_load(status.dwMemoryLoad) : MemoryPressure::NORMAL;
#elif defined(__linux__)
    std::array<char, PROC_FILE_BYTES> buffer;
    const std::string_view psi = proc_file("/proc/pressure/memory", buffer);
    if (!psi.empty()) return pressure_of_linux_psi(psi);
    return pressure_of_linux_meminfo(proc_file("/proc/meminfo", buffer));
#else
    return MemoryPressure::NORMAL;   // no signal here (the browser module, other systems)
#endif
}
} // namespace IrisSlide
