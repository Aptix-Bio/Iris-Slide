//
//  IrisSlideJpegFrames.hpp
//  Iris
//
//  One JPEG decoder for the whole library, by libjpeg-turbo: source frames (DICOM
//  1.2.840.10008.1.2.4.50, .51, .70) for the encoder, and JPEG tiles and associated images for the
//  reader, so both refuse a stream on the same terms.
//
#ifndef IrisSlideJpegFrames_hpp
#define IrisSlideJpegFrames_hpp
#include <cstdint>
#include <span>

#include "IrisTypes.hpp"
#include "IrisSlideGeometry.hpp"

namespace IrisSlide {
// RTM Dicom_05
/// Decode one 8-bit JPEG stream into @p pixels, interleaved in @p format (RGB, BGR, RGBA or BGRA;
/// alpha is opaque). The size is read from the stream's own header and must equal @p expected;
/// @p pixels must hold exactly columns × rows × the format's bytes. Throws std::runtime_error
/// naming both sizes on a mismatch, the format when it is not one of the four, the precision when it
/// is not 8 bits, and libjpeg-turbo's message when the stream is malformed or truncated (a warning
/// is an error, TJPARAM_STOPONWARNING).
void decode_jpeg (std::span<const std::uint8_t> stream, FrameShape expected, Iris::Format format,
                  std::span<std::uint8_t> pixels);

/// A source frame: decode_jpeg into interleaved 8-bit RGB.
void decode_jpeg_frame (std::span<const std::uint8_t> stream, FrameShape expected, std::span<std::uint8_t> rgb);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideJpegFrames_hpp */
