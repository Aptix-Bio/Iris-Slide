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
