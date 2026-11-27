//
//  IrisSlideFrameCodec.hpp
//  Iris
//
//  The one funnel from a source frame's compressed bytes to its pixels: a source module reads the
//  bytes, decode_frame decodes them by the codec they declare. A codec is one case here and one
//  module behind it (JPEG 2000 and HTJ2K: IrisSlideOpenJpeg; JPEG: IrisSlideJpegFrames).
//
#ifndef IrisSlideFrameCodec_hpp
#define IrisSlideFrameCodec_hpp
#include <cstdint>
#include <span>

#include "IrisSlideOpenJpeg.hpp"
#include "IrisSlideSourceCodec.hpp"

namespace IrisSlide {
// RTM Dicom_05
/// Whether decode_frame decodes @p codec.
bool is_decodable (SourceCodec codec) noexcept;
/// Decode one frame of @p codec into @p rgb, interleaved RGB8 of @p shape (sized from the
/// codestream's own header and compared with @p shape). Throws std::runtime_error naming the codec
/// when it is not one this build decodes, and the codec module's refusal otherwise.
void decode_frame (SourceCodec codec, std::span<const std::uint8_t> bytes, FrameShape shape,
                   std::span<std::uint8_t> rgb);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideFrameCodec_hpp */
