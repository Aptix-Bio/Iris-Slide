//
//  IrisSlideMemoryPressure.hpp
//  Iris
//
//  The operating system's memory pressure, read so an encode holds less while other programs need
//  memory (D26): the budget is the ceiling, and under pressure the encoder admits new rows against
//  a lower limit, down to the smallest the slide can keep to. One probe per operating system,
//  side by side in one function (IrisSlideMemoryPressure.cpp).
//
#ifndef IrisSlideMemoryPressure_hpp
#define IrisSlideMemoryPressure_hpp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace IrisSlide {
// RTM Encode_11
/// How hard the system is pressed for memory, from the operating system's own signal.
enum class MemoryPressure : std::uint8_t {
    NORMAL,     ///< no pressure, or no signal on this system
    WARNING,    ///< memory is short: hold less
    CRITICAL,   ///< memory is very short: hold the least an encode can
};
/// How long one reading of the pressure stands before the encoder reads it again.
constexpr auto MEMORY_PRESSURE_INTERVAL = std::chrono::milliseconds(250);
/// The share of the budget an encode keeps to under WARNING: one half.
constexpr std::uint64_t WARNING_BUDGET_DIVISOR = 2;

/// The operating system's memory pressure now: macOS's kern.memorystatus_vm_pressure_level,
/// Linux's pressure stall information (/proc/pressure/memory, or MemAvailable in /proc/meminfo
/// where the kernel has none), Windows' memory load (GlobalMemoryStatusEx). NORMAL where the
/// system gives no signal or it cannot be read, since pressure only lowers what an encode holds.
MemoryPressure memory_pressure () noexcept;

/// The limit an encode admits new rows against under @p pressure: @p budget when NORMAL, half of
/// it when WARNING, and @p smallest when CRITICAL; @p smallest, the least the slide can keep to, is
/// the floor in every case, so an encode always progresses.
constexpr std::uint64_t pressured_budget (std::uint64_t budget, std::uint64_t smallest, MemoryPressure pressure) noexcept
{
    switch (pressure) {
        case MemoryPressure::NORMAL:   return std::max(budget, smallest);
        case MemoryPressure::WARNING:  return std::max(budget / WARNING_BUDGET_DIVISOR, smallest);
        case MemoryPressure::CRITICAL: return smallest;
    }
    return smallest;   // unreachable: the switch names every enumerator
}

// Each system's reading turned into a level, apart from where it is read, so every system's rule
// is tested on every system. Each reads NORMAL from a signal it cannot parse.

/// macOS: kern.memorystatus_vm_pressure_level, 2 or more WARNING and 4 or more CRITICAL.
MemoryPressure pressure_of_macos_level (int level) noexcept;
/// Windows: GlobalMemoryStatusEx's dwMemoryLoad, the percentage of physical memory in use.
MemoryPressure pressure_of_windows_load (unsigned long load) noexcept;
/// Linux: the text of /proc/pressure/memory, its `full` and `some` lines' avg10.
MemoryPressure pressure_of_linux_psi (std::string_view psi) noexcept;
/// Linux without pressure stall information: the text of /proc/meminfo, MemAvailable as a share
/// of MemTotal; NORMAL when either field is absent or unreadable.
MemoryPressure pressure_of_linux_meminfo (std::string_view meminfo) noexcept;
} // namespace IrisSlide
#endif /* IrisSlideMemoryPressure_hpp */
