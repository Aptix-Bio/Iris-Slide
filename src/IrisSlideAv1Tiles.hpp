//
//  IrisSlideAv1Tiles.hpp
//  Iris
//
//  AV1 tiles (ARCHITECTURE D9, D22): a 256-pixel RGBA8 tile's focal planes as one bare AV1
//  stream, BT.601 at full range, 8-bit 4:2:0, encoded by libaom on one thread, so the same pixels
//  and quality give the same bytes on a given build and CPU path (D24).
//
#ifndef IrisSlideAv1Tiles_hpp
#define IrisSlideAv1Tiles_hpp
#include <cstdint>
#include <span>
#include <vector>

#include "IrisSlideGeometry.hpp"

namespace IrisSlide {
// RTM Encode_13
/// The bytes of one plane of a tile in 8-bit 4:2:0: Y, then U, then V.
constexpr std::size_t I420_TILE_BYTES = std::size_t{RECUT_TILE} * RECUT_TILE * 3 / 2;
/// The quality scale callers pass (QUALITY_MIN to QUALITY_MAX) and libaom's quantiser range
/// (0, the finest, to 63), between which encode_av1_tile maps linearly.
constexpr std::uint32_t AV1_QUANTISER_MAX = 63;
/// libaom's speed setting in good-quality mode: 0 is the slowest and finest, 6 the fastest of
/// that mode *(speed and size on stained tissue to measure)*.
constexpr int AV1_CPU_USED = 6;

/// @p rgba (RECUT_TILE² × 4 bytes) as 8-bit 4:2:0 with BT.601 coefficients at full range, the
/// colour D22 states: libyuv's ABGRToJ420, whose ABGR is R, G, B, A in memory. Throws
/// std::invalid_argument when @p rgba is not a tile's size.
std::vector<std::uint8_t> tile_to_i420 (std::span<const std::uint8_t> rgba);

/// A tile's focal planes as one bare AV1 stream (D9, D22), @p planes in stream order: the
/// in-focus plane first, then the others outward from it (+1, -1, +2, -2, ...), as the caller
/// orders them; a tile of one plane is a stack of one. Each plane is one temporal unit: a temporal
/// delimiter, then, in the first unit only, the sequence header (profile Main, 8-bit 4:2:0,
/// BT.601 matrix coefficients at full range; for one plane the full header rather than the
/// reduced still-picture one), then the frame, each OBU with its size field. The first plane is
/// the key frame and every later plane an inter frame predicted from the ones before it. @p
/// quality is QUALITY_MIN to QUALITY_MAX, mapped onto libaom's quantiser range. Throws
/// std::invalid_argument for no planes, a wrong-sized plane or a quality outside its range, and
/// std::runtime_error naming libaom's error when an encode fails.
std::vector<std::uint8_t> encode_av1_tile (std::span<const std::span<const std::uint8_t>> planes, std::uint32_t quality);
} // namespace IrisSlide
#endif /* IrisSlideAv1Tiles_hpp */
