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

/// out[v] = saturate((Σk W[k]·rows[k][v] + half) >> Shift): one pass of the reduction over count
/// contiguous values, from In samples to Out (8 to 16 bits for the first pass, 16 to 8 for the second).
template <typename In, typename Out, int Shift>
HWY_INLINE void weighted_row (const In* const* rows, Out* HWY_RESTRICT out, std::size_t count)
{
    const hn::ScalableTag<std::int32_t> d32;
    const hn::Rebind<In, decltype(d32)> d_in;
    const hn::Rebind<Out, decltype(d32)> d_out;
    const std::size_t lanes = hn::Lanes(d32);
    const auto half = hn::Set(d32, 1 << (Shift - 1));
    std::size_t v = 0;
    for (; v + lanes <= count; v += lanes) {
        auto sum = half;
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k)
            sum = hn::Add(sum, hn::Mul(hn::PromoteTo(d32, hn::LoadU(d_in, rows[k] + v)), hn::Set(d32, REDUCE_WEIGHTS[k])));
        // DemoteTo saturates: the second pass's clamp to 0-255.
        hn::StoreU(hn::DemoteTo(d_out, hn::ShiftRight<Shift>(sum)), d_out, out + v);
    }
    for (; v < count; ++v) out[v] = detail::tap_sum<Out, Shift>([&](std::uint32_t k) { return rows[k][v]; });
}

// RTM Pyramid_04
void first_pass_row (const std::uint8_t* const* rows, std::int16_t* HWY_RESTRICT out, std::size_t count)
{
    weighted_row<std::uint8_t, std::int16_t, REDUCE_FIRST_SHIFT>(rows, out, count);
}
void second_pass_row (const std::int16_t* const* rows, std::uint8_t* HWY_RESTRICT out, std::size_t count)
{
    weighted_row<std::int16_t, std::uint8_t, REDUCE_SECOND_SHIFT>(rows, out, count);
}

const char* reduce_target_name ()
{
    return hwy::TargetName(HWY_TARGET);
}
} // namespace HWY_NAMESPACE
} // END IRIS SLIDE NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace IrisSlide {
HWY_EXPORT(first_pass_row);
HWY_EXPORT(second_pass_row);
HWY_EXPORT(reduce_target_name);

namespace {
constexpr std::size_t REGION_VALUES = std::size_t{REDUCE_REGION} * RGBA_BYTES;   // values in a region row
constexpr std::size_t TILE_VALUES   = std::size_t{RECUT_TILE} * RGBA_BYTES;      // values in a tile row
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
        HWY_DYNAMIC_DISPATCH(first_pass_row)(rows.data(), first.data() + std::size_t{j} * REGION_VALUES, REGION_VALUES);
    }
    // Transpose by pixel (4 values): column x of the first pass becomes row x.
    std::vector<std::int16_t> transposed(std::size_t{REDUCE_REGION} * TILE_VALUES);
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::uint32_t x = 0; x < REDUCE_REGION; ++x)
            std::memcpy(&transposed[std::size_t{x} * TILE_VALUES + std::size_t{j} * RGBA_BYTES],
                        &first[std::size_t{j} * REGION_VALUES + std::size_t{x} * RGBA_BYTES], RGBA_BYTES * sizeof(std::int16_t));
    // Second pass, the horizontal one as a vertical pass over the transposed rows.
    std::vector<std::uint8_t> second(std::size_t{RECUT_TILE} * TILE_VALUES);
    for (std::uint32_t i = 0; i < RECUT_TILE; ++i) {
        std::array<const std::int16_t*, REDUCE_TAPS> rows {};
        for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) rows[k] = transposed.data() + (2 * std::size_t{i} + k) * TILE_VALUES;
        HWY_DYNAMIC_DISPATCH(second_pass_row)(rows.data(), second.data() + std::size_t{i} * TILE_VALUES, TILE_VALUES);
    }
    // Transpose back: row i of the second pass is output column i.
    for (std::uint32_t i = 0; i < RECUT_TILE; ++i)
        for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
            std::memcpy(&tile[std::size_t{j} * TILE_VALUES + std::size_t{i} * RGBA_BYTES],
                        &second[std::size_t{i} * TILE_VALUES + std::size_t{j} * RGBA_BYTES], RGBA_BYTES);
}

void reduce_2x_reference (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile)
{
    check_sizes(region, tile);
    std::vector<std::int16_t> first(std::size_t{RECUT_TILE} * REGION_VALUES);
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::size_t v = 0; v < REGION_VALUES; ++v)
            first[std::size_t{j} * REGION_VALUES + v] = detail::tap_sum<std::int16_t, REDUCE_FIRST_SHIFT>(
                [&](std::uint32_t k) { return region[(2 * std::size_t{j} + k) * REGION_VALUES + v]; });
    // Second pass, horizontal: output value (j, i, c) reads first-pass row j at pixels 2i to 2i+7.
    for (std::uint32_t j = 0; j < RECUT_TILE; ++j)
        for (std::size_t value = 0; value < TILE_VALUES; ++value) {
            const std::size_t i = value / RGBA_BYTES, c = value % RGBA_BYTES;
            tile[std::size_t{j} * TILE_VALUES + value] = detail::tap_sum<std::uint8_t, REDUCE_SECOND_SHIFT>(
                [&](std::uint32_t k) { return first[std::size_t{j} * REGION_VALUES + (2 * i + k) * RGBA_BYTES + c]; });
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
        std::memset(tile.data() + std::size_t{y} * TILE_VALUES + std::size_t{from} * RGBA_BYTES, WHITE,
                    std::size_t{RECUT_TILE - from} * RGBA_BYTES);
    }
}

const char* reduce_2x_target () noexcept
{
    return HWY_DYNAMIC_DISPATCH(reduce_target_name)();
}

std::vector<std::int64_t> reduce_2x_targets ()
{
    return hwy::SupportedAndGeneratedTargets();   // this file's targets: every attainable one
}
} // END IRIS SLIDE NAMESPACE
#endif // HWY_ONCE
