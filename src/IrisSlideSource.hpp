//
//  IrisSlideSource.hpp
//  Iris
//
//  The rebuilt encoder's one way in (ARCHITECTURE D14): every slide it encodes, whatever its
//  container and codec, is a Source, and open_source is the only code that knows file formats.
//  A source kind is one row of open_source's table and one module; removing a kind removes its
//  row and its module, and nothing else in the encoder changes.
//
#ifndef IrisSlideSource_hpp
#define IrisSlideSource_hpp
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "IrisSlideReduce.hpp"

namespace IrisSlide {
// RTM Encode_10
/// What a source says about its pixels' physical size and colour, at its highest resolution.
/// Patient and acquisition attributes are not read: the encoder writes only the caller's I2S
/// attributes (decided 2026-11-27, D2).
struct SourceMeasurements {
    double      microns_per_pixel = 0.0;   ///< 0 when the source does not say
    double      magnification     = 0.0;   ///< the objective's power; 0 when the source does not say
    std::string icc_profile;               ///< ICC.1 bytes, verbatim; empty when the source has none
};

/// A slide's highest-resolution layer, read as 256-pixel RGBA8 tiles. read_tile is the whole of
/// a tile's reading: one call takes the source data under the tile, decodes it and assembles the
/// tile, so a worker that claims a tile does all of it on its own thread. It is safe to call from
/// any number of threads at once, for any tiles, in any order.
class Source {
public:
    virtual ~Source () = default;
    /// The layer's size in pixels.
    virtual LayerSize size () const noexcept = 0;
    virtual const SourceMeasurements& measurements () const noexcept = 0;
    /// Tile (x, y) into @p rgba (RECUT_TILE² × 4 bytes), white beyond the slide. Throws
    /// std::runtime_error naming what did not read or decode.
    virtual void read_tile (std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) = 0;
    /// The most memory one read_tile call holds while it runs, beyond the tile it writes: the
    /// encoder charges it to its byte budget for every tile being read.
    virtual std::uint64_t read_scratch_bytes () const noexcept = 0;
};

/// The Source for the slide at @p path: the first row of the source table whose kind recognises
/// the file opens it. Throws std::runtime_error naming the path and every kind that declined it.
std::unique_ptr<Source> open_source (const std::filesystem::path& path);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideSource_hpp */
