//
//  IrisSlideReduce.hpp
//  Iris
//
//  The 2x reduction that derives each lower layer (ARCHITECTURE D15, D16): a Mitchell-Netravali
//  cubic widened by 2, 8 taps per axis, in fixed-point arithmetic, so the scalar reference and
//  every Highway target give the same bytes.
//
#ifndef IrisSlideReduce_hpp
#define IrisSlideReduce_hpp
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "IrisSlideRetile.hpp"

namespace IrisSlide {
// RTM Pyramid_01
/// Output pixel i of a reduced row reads source pixels 2i-3 to 2i+4: 3 before its pair (2i, 2i+1)
/// and 3 after it.
constexpr std::uint32_t REDUCE_HALO_BEFORE = 3;
constexpr std::uint32_t REDUCE_HALO_AFTER  = 3;
constexpr std::uint32_t REDUCE_TAPS        = REDUCE_HALO_BEFORE + 2 + REDUCE_HALO_AFTER;   // the pair and its halo
/// The side of the source region a 256-pixel reduced tile reads: 3 + 512 + 3 pixels.
constexpr std::uint32_t REDUCE_REGION = 2 * RECUT_TILE + REDUCE_HALO_BEFORE + REDUCE_HALO_AFTER;
/// The memory one reduce_2x call and its gathered region hold while they run: the region (RGBA8),
/// the first pass and its transpose (int16), and the second pass (RGBA8).
constexpr std::uint64_t REDUCE_SCRATCH_BYTES =
    std::uint64_t{REDUCE_REGION} * REDUCE_REGION * RGBA_BYTES + 2 * std::uint64_t{RECUT_TILE} * REDUCE_REGION * RGBA_BYTES * 2 +
    std::uint64_t{RECUT_TILE} * RECUT_TILE * RGBA_BYTES;
/// The weights are integers summing to 2^REDUCE_WEIGHT_BITS. The first (vertical) pass keeps
/// REDUCE_WEIGHT_BITS - REDUCE_FIRST_SHIFT fractional bits in signed 16 bits; the second
/// (horizontal) pass shifts out the rest, rounding half up, and clamps to 0-255.
constexpr int REDUCE_WEIGHT_BITS  = 14;
constexpr int REDUCE_FIRST_SHIFT  = 8;
constexpr int REDUCE_SECOND_SHIFT = 2 * REDUCE_WEIGHT_BITS - REDUCE_FIRST_SHIFT;
/// The Mitchell-Netravali family's parameters (Q16: B = C = 1/3 to start, decided 2026-11-27).
constexpr double MITCHELL_B = 1.0 / 3.0;
constexpr double MITCHELL_C = 1.0 / 3.0;

/// A value one pass of the reduction reads or writes: the 8-bit pixels, or the signed 16-bit
/// first pass the second reads.
template <typename T>
concept PassValue = std::same_as<T, std::uint8_t> || std::same_as<T, std::int16_t>;

/// A callable giving the RGBA8 pixels of tile (x, y) of a layer (RECUT_TILE² × 4 bytes).
template <typename TileOf>
concept TileLookup = requires(TileOf tile_of, std::uint32_t x, std::uint32_t y) {
    { tile_of(x, y) } -> std::convertible_to<std::span<const std::uint8_t>>;
};

/// The REDUCE_REGION² RGBA8 region reduced tile (out_x, out_y) reads from @p layer, into
/// @p region: source pixels from 512·out - 3 to 512·out + 514 on each axis, with coordinates
/// outside the layer clamped to its edge, so the edge pixel repeats. Throws std::runtime_error
/// when @p region is the wrong size or a tile within the layer is missing.
template <TileLookup TileOf>
void gather_region (LayerSize layer, std::uint32_t out_x, std::uint32_t out_y, TileOf&& tile_of,
                    std::span<std::uint8_t> region);

/// Pixels of @p tile, reduced tile (out_x, out_y) of a layer of size @p layer, that lie beyond the
/// layer's width or height are set white: a partial tile is white beyond the slide in every layer.
void whiten_beyond (LayerSize layer, std::uint32_t out_x, std::uint32_t out_y, std::span<std::uint8_t> tile);

/// The 256-pixel RGBA8 tile reduced from @p region (REDUCE_REGION² × 4 bytes) by the Highway
/// kernel for the best target the CPU runs.
void reduce_2x (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile);
/// The same reduction, one value at a time: the reference every target is tested against.
void reduce_2x_reference (std::span<const std::uint8_t> region, std::span<std::uint8_t> tile);
/// The Highway target reduce_2x runs on, and the targets it was compiled for that the CPU runs.
const char* reduce_2x_target () noexcept;
std::vector<std::int64_t> reduce_2x_targets ();

// MARK: - Implementation

namespace detail {
constexpr double mitchell (double x)
{
    const double B = MITCHELL_B, C = MITCHELL_C;
    x = x < 0 ? -x : x;
    if (x < 1) return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6;
    if (x < 2) return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x +
                       (8 * B + 24 * C)) / 6;
    return 0;
}
constexpr std::int32_t round_to_int (double v)
{
    return static_cast<std::int32_t>(v >= 0 ? v + 0.5 : v - 0.5);
}
/// Tap k reads the source pixel whose centre is k - (REDUCE_HALO_BEFORE + 0.5) source pixels from
/// the output pixel's centre, which lies between its pair; the kernel is widened by 2 for a 2x
/// reduction. The rounded weights' shortfall from 2^REDUCE_WEIGHT_BITS goes to the two centre taps
/// (the pair, REDUCE_HALO_BEFORE and REDUCE_HALO_BEFORE + 1), so the sum is exact and the table
/// symmetric.
consteval std::array<std::int32_t, REDUCE_TAPS> make_weights ()
{
    std::array<double, REDUCE_TAPS> raw {};
    double sum = 0;
    for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) {
        raw[k] = mitchell((double(k) - (REDUCE_HALO_BEFORE + 0.5)) / 2.0);
        sum += raw[k];
    }
    std::array<std::int32_t, REDUCE_TAPS> weights {};
    std::int32_t total = 0;
    for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) {
        weights[k] = round_to_int(raw[k] / sum * double(1 << REDUCE_WEIGHT_BITS));
        total += weights[k];
    }
    const std::int32_t shortfall = (1 << REDUCE_WEIGHT_BITS) - total;   // even: the table is symmetric
    weights[REDUCE_HALO_BEFORE] += shortfall / 2;
    weights[REDUCE_HALO_BEFORE + 1] += shortfall / 2;
    return weights;
}
} // namespace detail

// RTM Pyramid_01
constexpr std::array<std::int32_t, REDUCE_TAPS> REDUCE_WEIGHTS = detail::make_weights();
constexpr std::int32_t REDUCE_WEIGHT_SUM = [] {
    std::int32_t sum = 0;
    for (const auto w : REDUCE_WEIGHTS) sum += w;
    return sum;
}();
static_assert(REDUCE_WEIGHT_SUM == (1 << REDUCE_WEIGHT_BITS), "the reduction's weights sum to the fixed-point unit");

namespace detail {
/// One output value of a pass: the weighted sum of the REDUCE_TAPS samples sample(k) gives,
/// rounded, shifted right by Shift and saturated to Out, as Highway's DemoteTo saturates, so the
/// vector kernels' scalar tails and the reference compute it the same way.
template <PassValue Out, int Shift, typename Sample>
    requires std::invocable<Sample, std::uint32_t>
Out tap_sum (Sample&& sample)
{
    std::int32_t sum = 1 << (Shift - 1);
    for (std::uint32_t k = 0; k < REDUCE_TAPS; ++k) sum += REDUCE_WEIGHTS[k] * std::int32_t{sample(k)};
    return static_cast<Out>(std::clamp<std::int32_t>(sum >> Shift, std::numeric_limits<Out>::min(), std::numeric_limits<Out>::max()));
}
} // namespace detail

// RTM Pyramid_03
template <TileLookup TileOf>
void gather_region (LayerSize layer, std::uint32_t out_x, std::uint32_t out_y, TileOf&& tile_of,
                    std::span<std::uint8_t> region)
{
    if (region.size() != std::size_t{REDUCE_REGION} * REDUCE_REGION * RGBA_BYTES)
        throw std::runtime_error("a reduction region needs " +
                                 std::to_string(std::size_t{REDUCE_REGION} * REDUCE_REGION * RGBA_BYTES) + " bytes, given " +
                                 std::to_string(region.size()));
    if (layer.width == 0 || layer.height == 0) throw std::runtime_error("a reduction read an empty layer");
    const auto clamp = [](std::int64_t v, std::uint64_t size) {
        return static_cast<std::uint64_t>(std::clamp<std::int64_t>(v, 0, std::int64_t(size) - 1));
    };
    const std::int64_t left = std::int64_t{out_x} * 2 * RECUT_TILE - REDUCE_HALO_BEFORE;
    const std::int64_t top  = std::int64_t{out_y} * 2 * RECUT_TILE - REDUCE_HALO_BEFORE;
    for (std::uint32_t ry = 0; ry < REDUCE_REGION; ++ry) {
        const std::uint64_t sy = clamp(top + ry, layer.height);
        std::uint8_t* out = region.data() + std::size_t{ry} * REDUCE_REGION * RGBA_BYTES;
        for (std::uint32_t rx = 0; rx < REDUCE_REGION;) {
            const std::uint64_t sx = clamp(left + rx, layer.width);
            const std::span<const std::uint8_t> tile = tile_of(static_cast<std::uint32_t>(sx / RECUT_TILE),
                                                               static_cast<std::uint32_t>(sy / RECUT_TILE));
            if (tile.size() != std::size_t{RECUT_TILE} * RECUT_TILE * RGBA_BYTES)
                throw std::runtime_error("a reduction needs tile (" + std::to_string(sx / RECUT_TILE) + ", " +
                                         std::to_string(sy / RECUT_TILE) + ") of its layer, which was not given");
            const std::uint8_t* row = tile.data() + (sy % RECUT_TILE) * RECUT_TILE * RGBA_BYTES;
            // Inside the layer, copy the run up to the end of this source tile or of the layer;
            // outside it, the clamped edge pixel is copied one at a time.
            const bool inside = left + rx >= 0 && std::uint64_t(left + rx) < layer.width;
            const std::uint32_t run = inside ? static_cast<std::uint32_t>(std::min<std::uint64_t>(
                {RECUT_TILE - sx % RECUT_TILE, layer.width - sx, REDUCE_REGION - rx})) : 1;
            std::memcpy(out + std::size_t{rx} * RGBA_BYTES, row + (sx % RECUT_TILE) * RGBA_BYTES, std::size_t{run} * RGBA_BYTES);
            rx += run;
        }
    }
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideReduce_hpp */
