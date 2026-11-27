//
//  IrisSlideOpenJpeg.hpp
//  Iris
//
//  JPEG 2000 and HTJ2K source frames, decoded by OpenJPEG (ARCHITECTURE D17).
//
#ifndef IrisSlideOpenJpeg_hpp
#define IrisSlideOpenJpeg_hpp
#include <cstdint>
#include <span>

#include "IrisSlideGeometry.hpp"

namespace IrisSlide {
/// Decode one JPEG 2000 or HTJ2K codestream (a raw J2K codestream or a JP2 file) into @p rgb as
/// interleaved 8-bit RGB, rows top to bottom. The frame's size is read from the codestream's own
/// header and must equal @p expected; @p rgb must hold exactly columns × rows × 3 bytes. A
/// reversible or irreversible colour transform signalled in the codestream is undone by OpenJPEG.
/// Throws std::runtime_error naming both sizes on a mismatch, the component layout when it is not
/// three unsigned 8-bit full-resolution components, and OpenJPEG's own message when the
/// codestream is malformed or truncated (decoded in strict mode, so a short stream is an error).
void decode_jpeg2000_frame (std::span<const std::uint8_t> codestream,
                            FrameShape expected,
                            std::span<std::uint8_t> rgb);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideOpenJpeg_hpp */
