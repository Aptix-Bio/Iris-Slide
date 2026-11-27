//
//  IrisSlideRetile.hpp
//  Iris
//
//  Re-cutting a source's frames into 256-pixel tiles (ARCHITECTURE D18), with the row spans
//  converted by a Highway kernel chosen at run time (D16).
//
#ifndef IrisSlideRetile_hpp
#define IrisSlideRetile_hpp
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "IrisSlideOpenJpeg.hpp"

namespace IrisSlide {
// RTM Encode_10
/// The side of the tiles a re-cut makes, in pixels: Iris-Slide's tile length.
constexpr std::uint32_t RECUT_TILE = 256;

/// A slide stored as frames in row-major order (a TILED_FULL DICOM instance): its total pixel
/// size and the size of every frame. Frames at the right and bottom edges are full-size; only the
/// pixels within width and height are the slide's.
struct SlideGrid {
    std::uint64_t width  = 0;
    std::uint64_t height = 0;
    FrameShape    frame;
};

/// A callable giving the interleaved RGB8 pixels of frame (column, row), frame.columns ×
/// frame.rows × 3 bytes.
template <typename FrameOf>
concept FrameLookup = requires(FrameOf frame_of, std::uint32_t column, std::uint32_t row) {
    { frame_of(column, row) } -> std::convertible_to<std::span<const std::uint8_t>>;
};

/// Tiles across and down a re-cut of @p grid makes.
constexpr std::uint32_t tiles_across (const SlideGrid& grid) noexcept
{
    return static_cast<std::uint32_t>((grid.width + RECUT_TILE - 1) / RECUT_TILE);
}
constexpr std::uint32_t tiles_down (const SlideGrid& grid) noexcept
{
    return static_cast<std::uint32_t>((grid.height + RECUT_TILE - 1) / RECUT_TILE);
}

/// Tile (tile_x, tile_y) of the 256-pixel re-cut of @p grid, written into @p rgba as RGBA8
/// (RECUT_TILE² × 4 bytes, alpha opaque): each row is the spans of the frames it crosses, and
/// what lies beyond the slide's width or height is white. Throws std::runtime_error naming the
/// sizes when @p rgba or a frame @p frame_of gives is not the size it must be.
template <FrameLookup FrameOf>
void recut_tile (const SlideGrid& grid, std::uint32_t tile_x, std::uint32_t tile_y,
                 FrameOf&& frame_of, std::span<std::uint8_t> rgba);

/// Interleaved RGB8 to RGBA8 with alpha opaque, by the Highway kernel for the best target the
/// CPU runs (HWY_DYNAMIC_DISPATCH). Throws std::runtime_error when @p rgba does not hold 4 bytes
/// for every 3 of @p rgb.
void rgb8_to_rgba8 (std::span<const std::uint8_t> rgb, std::span<std::uint8_t> rgba);
/// The same conversion one pixel at a time: the reference every Highway target is tested against.
void rgb8_to_rgba8_reference (std::span<const std::uint8_t> rgb, std::span<std::uint8_t> rgba);
/// The name of the Highway target rgb8_to_rgba8 runs on (hwy::TargetName).
const char* rgb8_to_rgba8_target () noexcept;
/// The Highway targets the kernel was compiled for that this CPU runs, each a value
/// hwy::SetSupportedTargetsForTest takes: the list the every-target test forces in turn.
std::vector<std::int64_t> rgb8_to_rgba8_targets ();

// MARK: - Implementation

template <FrameLookup FrameOf>
void recut_tile (const SlideGrid& grid, std::uint32_t tile_x, std::uint32_t tile_y,
                 FrameOf&& frame_of, std::span<std::uint8_t> rgba)
{
    constexpr std::size_t RGB = 3, RGBA = 4;
    constexpr std::uint8_t WHITE = 0xFF;   // white and opaque, in every channel
    const std::size_t tile_bytes  = std::size_t{RECUT_TILE} * RECUT_TILE * RGBA;
    const std::size_t frame_bytes = std::size_t{grid.frame.columns} * grid.frame.rows * RGB;
    if (rgba.size() != tile_bytes)
        throw std::runtime_error("a re-cut tile needs " + std::to_string(tile_bytes) + " bytes, given " +
                                 std::to_string(rgba.size()));
    const std::uint64_t left = std::uint64_t{tile_x} * RECUT_TILE;
    const std::uint64_t top  = std::uint64_t{tile_y} * RECUT_TILE;
    for (std::uint32_t row = 0; row < RECUT_TILE; ++row) {
        std::uint8_t* out = rgba.data() + std::size_t{row} * RECUT_TILE * RGBA;
        const std::uint64_t y = top + row;
        std::uint32_t filled = 0;
        while (y < grid.height && filled < RECUT_TILE && left + filled < grid.width) {
            const std::uint64_t x = left + filled;
            const auto column = static_cast<std::uint32_t>(x % grid.frame.columns);
            const auto count  = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                {grid.frame.columns - column, RECUT_TILE - filled, grid.width - x}));
            const std::span<const std::uint8_t> frame = frame_of(static_cast<std::uint32_t>(x / grid.frame.columns),
                                                                 static_cast<std::uint32_t>(y / grid.frame.rows));
            if (frame.size() != frame_bytes)
                throw std::runtime_error("a frame for the re-cut holds " + std::to_string(frame.size()) +
                                         " bytes where " + std::to_string(frame_bytes) + " are needed");
            const std::size_t line = y % grid.frame.rows;
            rgb8_to_rgba8(frame.subspan((line * grid.frame.columns + column) * RGB, std::size_t{count} * RGB),
                          std::span(out + std::size_t{filled} * RGBA, std::size_t{count} * RGBA));
            filled += count;
        }
        std::memset(out + std::size_t{filled} * RGBA, WHITE, std::size_t{RECUT_TILE - filled} * RGBA);
    }
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideRetile_hpp */
