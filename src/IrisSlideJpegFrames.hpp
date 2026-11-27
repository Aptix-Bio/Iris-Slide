//
//  IrisSlideJpegFrames.hpp
//  Iris
//
//  JPEG source frames (DICOM 1.2.840.10008.1.2.4.50, .51, .70), decoded by libjpeg-turbo.
//
#ifndef IrisSlideJpegFrames_hpp
#define IrisSlideJpegFrames_hpp
#include <cstdint>
#include <span>

#include "IrisSlideGeometry.hpp"

namespace IrisSlide {
// RTM Dicom_05
/// Decode one 8-bit JPEG stream into @p rgb as interleaved 8-bit RGB. The frame's size is read from
/// the stream's own header and must equal @p expected; @p rgb must hold exactly columns × rows × 3
/// bytes. Throws std::runtime_error naming both sizes on a mismatch, the precision when it is not
/// 8 bits, and libjpeg-turbo's message when the stream is malformed or truncated (a warning is an
/// error, TJPARAM_STOPONWARNING).
void decode_jpeg_frame (std::span<const std::uint8_t> stream, FrameShape expected, std::span<std::uint8_t> rgb);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideJpegFrames_hpp */
