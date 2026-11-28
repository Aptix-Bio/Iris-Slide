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
#include "IrisSlideTypes.hpp"

namespace IrisSlide {
// RTM Encode_13
/// The bytes of one plane of a tile in 8-bit 4:2:0: Y, then U, then V.
constexpr std::size_t I420_TILE_BYTES = std::size_t{RECUT_TILE} * RECUT_TILE * 3 / 2;
/// The AV1 sequence profile the tiles are coded in: Main, 8- or 10-bit 4:2:0 (AV1 specification
/// 6.4.1, seq_profile 0), and the bit depth they are coded at (D22).
constexpr unsigned AV1_PROFILE_MAIN = 0;
constexpr unsigned AV1_BIT_DEPTH    = 8;
/// The quality scale callers pass (QUALITY_MIN to QUALITY_MAX) and libaom's quantiser range
/// (0, the finest, to 63), between which encode_av1_tile maps linearly.
constexpr std::uint32_t AV1_QUANTISER_MAX = 63;
// RTM Encode_16
/// libaom's fastest speed setting in good-quality mode (0 is its slowest). An encode's effort
/// counts down from it, so EFFORT_MIN is libaom's 6 and EFFORT_MAX its 0 (decided 2026-11-27):
/// one five-plane tissue tile at quality 90 took 87 ms and 37.4 KB at 6, about 1 s and 35.6 KB at 1
/// (macOS arm64, 2026-11-27).
constexpr int AV1_FASTEST_CPU_USED = 6;
static_assert(AV1_FASTEST_CPU_USED == EFFORT_MAX, "each effort is one of libaom's good-quality speeds");

/// @p rgba (RECUT_TILE² × 4 bytes) as 8-bit 4:2:0 with BT.601 coefficients at full range, the
/// colour D22 states: libyuv's ABGRToJ420, whose ABGR is R, G, B, A in memory. Throws
/// std::invalid_argument when @p rgba is not a tile's size.
std::vector<std::uint8_t> tile_to_i420 (std::span<const std::uint8_t> rgba);

/// A tile's focal planes as one bare AV1 stream (D9, D22), @p planes in the order they are coded:
/// Z order, the first plane first (Q24, resolved 2026-11-27); a tile of one plane is a stack of
/// one. Each plane is one temporal unit: a temporal
/// delimiter, then, in the first unit only, the sequence header (profile Main, 8-bit 4:2:0,
/// BT.601 matrix coefficients at full range; for one plane the full still-picture header, which
/// every decoder reads), then the frame, each OBU with its size field. The first plane is
/// the key frame and every later plane an inter frame predicted from the ones before it. @p
/// quality is QUALITY_MIN to QUALITY_MAX, mapped onto libaom's quantiser range, and @p effort
/// EFFORT_MIN to EFFORT_MAX, libaom's speed AV1_FASTEST_CPU_USED - effort. Throws
/// std::invalid_argument for no planes, a wrong-sized plane, or a quality or effort outside its
/// range, and std::runtime_error naming libaom's error when an encode fails.
std::vector<std::uint8_t> encode_av1_tile (std::span<const std::span<const std::uint8_t>> planes, std::uint32_t quality,
                                           Effort effort = EFFORT_DEFAULT);
} // namespace IrisSlide
#endif /* IrisSlideAv1Tiles_hpp */
