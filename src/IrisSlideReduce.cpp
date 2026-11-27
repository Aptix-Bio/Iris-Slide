//
//  IrisSlideReduce.cpp
//  Iris
//
//  The reduction's two passes are one Highway kernel each, compiled for every attainable target
//  (foreach_target.h) and chosen at run time (HWY_DYNAMIC_DISPATCH). Both passes are the same
//  shape, an 8-row weighted sum over contiguous values, so the second (horizontal) pass runs on
//  the first pass's output transposed, and its result is transposed back. Integer arithmetic makes
//  the order of the sums irrelevant: every target and the reference give the same bytes.
//
#ifndef HWY_COMPILE_ALL_ATTAINABLE
#define HWY_COMPILE_ALL_ATTAINABLE
#endif
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "IrisSlideReduce.cpp"
#include <hwy/foreach_target.h>   // must come before highway.h
#include <hwy/highway.h>

#include "IrisSlideReduce.hpp"

HWY_BEFORE_NAMESPACE();
namespace IrisSlide {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// RTM Pyramid_04
/// out[v] = (Σk W[k]·rows[k][v] + half) >> REDUCE_FIRST_SHIFT, from 8-bit samples to 16-bit.
void FirstPassRow (const std::uint8_t* const* rows, std::int16_t* HWY_RESTRICT out, std::size_t count)
{
    const hn::ScalableTag<std::int32_t> d32;
    const hn::Rebind<std::uint8_t, decltype(d32)> d8;
    const hn::Rebind<std::int16_t, decltype(d32)> d16;
    const std::size_t lanes = hn::Lanes(d32);
    const auto half = hn::Set(d32, 1 << (REDUCE_FIRST_SHIFT - 1));
    std::size_t v = 0;
    for (; v + lanes <= count; v += lanes) {
        auto sum = half;
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k)
            sum = hn::Add(sum, hn::Mul(hn::PromoteTo(d32, hn::LoadU(d8, rows[k] + v)), hn::Set(d32, REDUCE_WEIGHTS[k])));
        hn::StoreU(hn::DemoteTo(d16, hn::ShiftRight<REDUCE_FIRST_SHIFT>(sum)), d16, out + v);
    }
    for (; v < count; ++v) {
        std::int32_t sum = 1 << (REDUCE_FIRST_SHIFT - 1);
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) sum += REDUCE_WEIGHTS[k] * std::int32_t{rows[k][v]};
        out[v] = static_cast<std::int16_t>(sum >> REDUCE_FIRST_SHIFT);
    }
}

/// out[v] = clamp((Σk W[k]·rows[k][v] + half) >> REDUCE_SECOND_SHIFT, 0, 255), 16-bit to 8-bit.
void SecondPassRow (const std::int16_t* const* rows, std::uint8_t* HWY_RESTRICT out, std::size_t count)
{
    const hn::ScalableTag<std::int32_t> d32;
    const hn::Rebind<std::int16_t, decltype(d32)> d16;
    const hn::Rebind<std::uint8_t, decltype(d32)> d8;
    const std::size_t lanes = hn::Lanes(d32);
    const auto half = hn::Set(d32, 1 << (REDUCE_SECOND_SHIFT - 1));
    std::size_t v = 0;
    for (; v + lanes <= count; v += lanes) {
        auto sum = half;
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k)
            sum = hn::Add(sum, hn::Mul(hn::PromoteTo(d32, hn::LoadU(d16, rows[k] + v)), hn::Set(d32, REDUCE_WEIGHTS[k])));
        // DemoteTo saturates, which is the clamp to 0-255.
        hn::StoreU(hn::DemoteTo(d8, hn::ShiftRight<REDUCE_SECOND_SHIFT>(sum)), d8, out + v);
    }
    for (; v < count; ++v) {
        std::int32_t sum = 1 << (REDUCE_SECOND_SHIFT - 1);
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) sum += REDUCE_WEIGHTS[k] * std::int32_t{rows[k][v]};
        out[v] = static_cast<std::uint8_t>(std::clamp(sum >> REDUCE_SECOND_SHIFT, 0, 255));
    }
}

const char* ReduceTargetName ()
{
    return hwy::TargetName(HWY_TARGET);
}
} // namespace HWY_NAMESPACE
} // END IRIS SLIDE NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace IrisSlide {
HWY_EXPORT(FirstPassRow);
HWY_EXPORT(SecondPassRow);
HWY_EXPORT(ReduceTargetName);

namespace {
constexpr std::size_t RGBA = 4;
constexpr std::size_t REGION_VALUES = std::size_t{REDUCE_REGION} * RGBA;   // values in a region row
constexpr std::size_t TILE_VALUES   = std::size_t{RECUT_TILE} * RGBA;      // values in a tile row
constexpr std::uint8_t WHITE = 0xFF;

void check_sizes (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile)
{
    if (region.size() != std::size_t{REDUCE_REGION} * REGION_VALUES || tile.size() != std::size_t{RECUT_TILE} * TILE_VALUES)
        throw std::runtime_error("a 2x reduction takes a " + std::to_string(REDUCE_REGION) + "-pixel region to a " +
                                 std::to_string(RECUT_TILE) + "-pixel tile: given " + std::to_string(region.size()) +
                                 " and " + std::to_string(tile.size()) + " bytes");
}
} // namespace

// RTM Pyramid_02, Pyramid_04
void reduce_2x (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile)
{
    check_sizes(region, tile);
    // First pass, vertical: output row j reads region rows 2j to 2j+7, every value of each.
    std::vector<std::int16_t> first(std::size_t{RECUT_TILE} * REGION_VALUES);
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j) {
        std::array<const std::uint8_t*, REDUCE_TAPS> rows {};
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) rows[k] = region.data() + (2 * std::size_t{j} + k) * REGION_VALUES;
        HWY_DYNAMIC_DISPATCH(FirstPassRow)(rows.data(), first.data() + std::size_t{j} * REGION_VALUES, REGION_VALUES);
    }
    // Transpose by pixel (4 values): column x of the first pass becomes row x.
    std::vector<std::int16_t> transposed(std::size_t{REDUCE_REGION} * TILE_VALUES);
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::uint32_t x = 0; x < REDUCE_REGION; ++x)
            std::memcpy(&transposed[std::size_t{x} * TILE_VALUES + std::size_t{j} * RGBA],
                        &first[std::size_t{j} * REGION_VALUES + std::size_t{x} * RGBA], RGBA * sizeof(std::int16_t));
    // Second pass, the horizontal one as a vertical pass over the transposed rows.
    std::vector<std::uint8_t> second(std::size_t{RECUT_TILE} * TILE_VALUES);
    for (std::uint32_t i = 0; i < RECUT_TILE; ++i) {
        std::array<const std::int16_t*, REDUCE_TAPS> rows {};
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) rows[k] = transposed.data() + (2 * std::size_t{i} + k) * TILE_VALUES;
        HWY_DYNAMIC_DISPATCH(SecondPassRow)(rows.data(), second.data() + std::size_t{i} * TILE_VALUES, TILE_VALUES);
    }
    // Transpose back: row i of the second pass is output column i.
    for (std::uint32_t i = 0; i < RECUT_TILE; ++i)
        for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
            std::memcpy(&tile[std::size_t{j} * TILE_VALUES + std::size_t{i} * RGBA],
                        &second[std::size_t{i} * TILE_VALUES + std::size_t{j} * RGBA], RGBA);
}

void reduce_2x_reference (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile)
{
    check_sizes(region, tile);
    std::vector<std::int16_t> first(std::size_t{RECUT_TILE} * REGION_VALUES);
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::size_t v = 0; v < REGION_VALUES; ++v) {
            std::int32_t sum = 1 << (REDUCE_FIRST_SHIFT - 1);
            for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k)
                sum += REDUCE_WEIGHTS[k] * std::int32_t{region[(2 * std::size_t{j} + k) * REGION_VALUES + v]};
            first[std::size_t{j} * REGION_VALUES + v] = static_cast<std::int16_t>(sum >> REDUCE_FIRST_SHIFT);
        }
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::uint32_t i = 0; i < RECUT_TILE; ++i)
            for (std::size_t c = 0; c < RGBA; ++c) {
                std::int32_t sum = 1 << (REDUCE_SECOND_SHIFT - 1);
                for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k)
                    sum += REDUCE_WEIGHTS[k] * std::int32_t{first[std::size_t{j} * REGION_VALUES + (2 * std::size_t{i} + k) * RGBA + c]};
                tile[std::size_t{j} * TILE_VALUES + std::size_t{i} * RGBA + c] =
                    static_cast<std::uint8_t>(std::clamp(sum >> REDUCE_SECOND_SHIFT, 0, 255));
            }
}

// RTM Pyramid_03
void whiten_beyond (LayerSize layer, std::uint32_t out_x, std::uint32_t out_y, std::span<std::uint8_t> tile)
{
    const std::uint64_t left = std::uint64_t{out_x} * RECUT_TILE, top = std::uint64_t{out_y} * RECUT_TILE;
    const auto columns = static_cast<std::uint32_t>(std::min<std::uint64_t>(RECUT_TILE, layer.width > left ? layer.width - left : 0));
    const auto rows    = static_cast<std::uint32_t>(std::min<std::uint64_t>(RECUT_TILE, layer.height > top ? layer.height - top : 0));
    for (std::uint32_t y = 0; y < RECUT_TILE; ++y) {
        const std::uint32_t from = y < rows ? columns : 0;
        std::memset(tile.data() + std::size_t{y} * TILE_VALUES + std::size_t{from} * RGBA, WHITE,
                    std::size_t{RECUT_TILE - from} * RGBA);
    }
}

const char* reduce_2x_target () noexcept
{
    return HWY_DYNAMIC_DISPATCH(ReduceTargetName)();
}

std::vector<std::int64_t> reduce_2x_targets ()
{
    return hwy::SupportedAndGeneratedTargets();   // this file's targets: every attainable one
}
} // END IRIS SLIDE NAMESPACE
#endif // HWY_ONCE
