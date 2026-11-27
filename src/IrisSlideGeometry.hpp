//
//  IrisSlideGeometry.hpp
//  Iris
//
//  The rebuilt encoder's shared geometry, defined once: the tile side, the bytes per pixel of its
//  two pixel formats, a source frame's shape, a layer's size, its tile counts, and the size of its
//  2x reduction.
//
#ifndef IrisSlideGeometry_hpp
#define IrisSlideGeometry_hpp
#include <cstddef>
#include <cstdint>

namespace IrisSlide {
/// The side of every tile the encoder makes, in pixels: Iris-Slide's tile length.
constexpr std::uint32_t RECUT_TILE = 256;
/// Bytes per pixel of interleaved 8-bit RGB (a decoded source frame) and RGBA (a tile).
constexpr std::size_t RGB_BYTES  = 3;
constexpr std::size_t RGBA_BYTES = 4;

// RTM Dicom_05
/// The pixel size an instance declares for each of its frames (DICOM Columns and Rows).
struct FrameShape {
    std::uint32_t columns = 0;
    std::uint32_t rows    = 0;
};

/// A layer's size in pixels.
struct LayerSize {
    std::uint64_t width  = 0;
    std::uint64_t height = 0;
};
/// Tiles across and down a layer of @p layer's size.
constexpr std::uint32_t tiles_across (LayerSize layer) noexcept
{
    return static_cast<std::uint32_t>((layer.width + RECUT_TILE - 1) / RECUT_TILE);
}
constexpr std::uint32_t tiles_down (LayerSize layer) noexcept
{
    return static_cast<std::uint32_t>((layer.height + RECUT_TILE - 1) / RECUT_TILE);
}
/// The size of the layer a 2x reduction of @p layer makes: output pixel i covers source pixels
/// 2i and 2i+1.
constexpr LayerSize reduced (LayerSize layer) noexcept
{
    return {.width = (layer.width + 1) / 2, .height = (layer.height + 1) / 2};
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideGeometry_hpp */
