// RTM Encode_11
// D26 (decided 2026-11-27): an encode's budget is its ceiling, and under the operating system's
// memory pressure it admits new rows against a lower limit, down to the least the slide needs.
#include <catch2/catch_test_macros.hpp>

#include "IrisSlideMemoryPressure.hpp"

using IrisSlide::MemoryPressure;
using IrisSlide::pressured_budget;

TEST_CASE("Pressure lowers the limit an encode admits rows against, down to the slide's least", "[Encode_11]")
{
    constexpr std::uint64_t BUDGET = 1000, SMALLEST = 300;
    STATIC_CHECK(pressured_budget(BUDGET, SMALLEST, MemoryPressure::NORMAL) == BUDGET);
    STATIC_CHECK(pressured_budget(BUDGET, SMALLEST, MemoryPressure::WARNING) == BUDGET / IrisSlide::WARNING_BUDGET_DIVISOR);
    STATIC_CHECK(pressured_budget(BUDGET, SMALLEST, MemoryPressure::CRITICAL) == SMALLEST);
    STATIC_CHECK(pressured_budget(500, SMALLEST, MemoryPressure::WARNING) == SMALLEST);   // half of 500 is below the least
}

TEST_CASE("The operating system's memory pressure reads as one of the three levels", "[Encode_11]")
{
    const MemoryPressure now = IrisSlide::memory_pressure();
    CHECK((now == MemoryPressure::NORMAL || now == MemoryPressure::WARNING || now == MemoryPressure::CRITICAL));
}

TEST_CASE("Each system's pressure signal reads as its level, and a signal that cannot be read as NORMAL", "[Encode_11]")
{
    using IrisSlide::pressure_of_linux_meminfo;
    using IrisSlide::pressure_of_linux_psi;
    CHECK(IrisSlide::pressure_of_macos_level(1) == MemoryPressure::NORMAL);
    CHECK(IrisSlide::pressure_of_macos_level(2) == MemoryPressure::WARNING);
    CHECK(IrisSlide::pressure_of_macos_level(4) == MemoryPressure::CRITICAL);
    CHECK(IrisSlide::pressure_of_windows_load(89) == MemoryPressure::NORMAL);
    CHECK(IrisSlide::pressure_of_windows_load(90) == MemoryPressure::WARNING);
    CHECK(IrisSlide::pressure_of_windows_load(97) == MemoryPressure::CRITICAL);

    // /proc/pressure/memory as the kernel writes it (Documentation/accounting/psi.rst).
    CHECK(pressure_of_linux_psi("some avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
                                "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n") == MemoryPressure::NORMAL);
    CHECK(pressure_of_linux_psi("some avg10=12.50 avg60=3.00 avg300=1.00 total=9\n"
                                "full avg10=2.00 avg60=0.50 avg300=0.10 total=3\n") == MemoryPressure::WARNING);
    CHECK(pressure_of_linux_psi("some avg10=40.00 avg60=3.00 avg300=1.00 total=9\n"
                                "full avg10=15.00 avg60=0.50 avg300=0.10 total=3\n") == MemoryPressure::CRITICAL);
    CHECK(pressure_of_linux_psi("some avg10=x avg60=0.00\nfull avg10=\n") == MemoryPressure::NORMAL);
    CHECK(pressure_of_linux_psi("") == MemoryPressure::NORMAL);

    CHECK(pressure_of_linux_meminfo("MemTotal:       1000 kB\nMemFree:  10 kB\nMemAvailable:    500 kB\n") == MemoryPressure::NORMAL);
    CHECK(pressure_of_linux_meminfo("MemTotal:       1000 kB\nMemAvailable:     80 kB\n") == MemoryPressure::WARNING);
    CHECK(pressure_of_linux_meminfo("MemTotal:       1000 kB\nMemAvailable:     40 kB\n") == MemoryPressure::CRITICAL);
    // A kernel without MemAvailable, or a field that does not parse, is no signal.
    CHECK(pressure_of_linux_meminfo("MemTotal:       1000 kB\nMemFree:  10 kB\n") == MemoryPressure::NORMAL);
    CHECK(pressure_of_linux_meminfo("MemTotal:       1000 kB\nMemAvailable:   n/a\n") == MemoryPressure::NORMAL);
}
