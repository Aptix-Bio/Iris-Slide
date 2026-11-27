//
//  IrisSlidePriv.hpp
//  Iris
//
//  Created by Ryan Landvater on 1/9/24.
//

#ifndef IrisSlidePriv_h
#define IrisSlidePriv_h
#ifndef IRIS_SLIDE_MAJOR_VERSION
#define IRIS_SLIDE_MAJOR_VERSION 2025
#endif
#ifndef IRIS_SLIDE_MINOR_VERSION
#define IRIS_SLIDE_MINOR_VERSION 3
#endif
#ifndef IRIS_SLIDE_BUILD_NUMBER
#define IRIS_SLIDE_BUILD_NUMBER  2
#endif
#ifndef FLT16_MIN
#define _Float16            float // use 32-bit if no 16
#endif
#ifndef U16_CAST
#define U16_CAST(X)         static_cast<uint16_t>(X)
#endif
#ifndef U32_CAST
#define U32_CAST(X)         static_cast<uint32_t>(X)
#endif
#ifndef F16_CAST
#define F16_CAST(X)         static_cast<_Float16>(X)
#endif
#ifndef F32_CAST
#define F32_CAST(X)         static_cast<float>(X)
#endif
#ifndef F64_CAST
#define F64_CAST(X)         static_cast<double>(X)
#endif
#ifndef IRIS_INCLUDE_OPENSLIDE
#define IRIS_INCLUDE_OPENSLIDE 0   // RTM Build_07: opt-in
#endif
#include <iostream>
#include <assert.h>
#include "IrisCore.hpp"
#include "IrisSlideCore.hpp"
#include "IrisBuffer.hpp"
#include "IrisQueue.hpp"
#include "IrisAsync.hpp"
#include "IrisFileExtension.hpp"
#include "IrisSlidePrivTypes.hpp"
#include "IrisSlideContext.hpp"
#include "IrisSlide.hpp"
#include "IrisSlideEncoder.hpp"

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>
namespace IrisSlide {
/// Every failure the codec reports: its own domain and code, the message
/// naming the offending value, and the caller's location.
inline Iris::Result codec_failure (SlideError code, std::string message,
                                   std::source_location where = std::source_location::current()) noexcept
{
    return Iris::Result::failure(Iris::Domain::IRIS_SLIDE, code, std::move(message), where);
}
/// Tile encodings the Iris format defines that Iris-Slide has no codec for yet: AV1
/// (TileEncodingAv1Refused_3.6, RTM Decode_09, ARCHITECTURE Q11) and the GPU block-compressed
/// UASTC and XUASTC (TileEncodingBlocksRefused_3.7, RTM Decode_10, ARCHITECTURE Q13, Q14), all
/// decided 2026-11-25. A tile declared one of them is refused by name instead of reaching a
/// decoder of another format. Returns the encoding's name, or nullptr for an encoding
/// Iris-Slide encodes and decodes.
inline const char* unbuilt_tile_encoding (Encoding encoding) noexcept
{
    switch (encoding) {
        case TILE_ENCODING_AV1:     return "TILE_ENCODING_AV1";
        case TILE_ENCODING_UASTC:   return "TILE_ENCODING_UASTC";
        case TILE_ENCODING_XUASTC:  return "TILE_ENCODING_XUASTC";
        case TILE_ENCODING_UNDEFINED:
        case TILE_ENCODING_IRIS:
        case TILE_ENCODING_JPEG:    return nullptr;
    }
    return nullptr;
}
/// The refusal of an unbuilt tile encoding: @p refusal names who refused and what ("compress_tile
/// refused"), and the message goes on to name the encoding.
inline std::string unbuilt_tiles_refused (const std::string& refusal, const char* encoding)
{
    return refusal + " " + encoding + ": Iris-Slide does not encode or decode tiles in this "
        "encoding yet (development/ARCHITECTURE.md Q11, Q13, Q14)";
}
// RTM Api_06
/// A failed Result from a library Iris-Slide calls (IFE), thrown inside Iris-Slide, carrying the
/// domain, code, message and place of the original, so a boundary that returns a Result returns
/// IFE's FileError as it came (ARCHITECTURE D12, decided 2026-11-26). No such boundary reaches
/// an IFE call yet: the reads that do (open, tile and image reads) return a Slide or a Buffer
/// until CreateForm_12.2 and ReadsReturnResult_12.3, and BoundaryHelper_12.1's one helper will
/// return it. Derived from
/// std::runtime_error so the message is copied without allocating, as an exception's must be,
/// and so a boundary that returns no Result still reads it through what().
class ResultFailure : public std::runtime_error {
public:
    explicit ResultFailure (const Iris::Result& result) :
        std::runtime_error(result.message), _domain(result.domain), _code(result.code), _where(result.where) {}

    [[nodiscard]] Iris::Result result () const noexcept
    {
        Iris::Result failed;   // a default Result is a failure
        failed.domain  = _domain;
        failed.code    = _code;
        failed.message = what();
        failed.where   = _where;
        return failed;
    }

private:
    Iris::Domain         _domain;
    std::uint32_t        _code;
    std::source_location _where;
};
/// IFE's read calls return a Result and never throw (IFE R-8, D30). Inside this library a failure
/// is thrown on as a ResultFailure, and the public function's boundary returns it.
inline void throw_if_failed (const Iris::Result& result)
{
    if (result.failed()) throw ResultFailure(result);
}
} // END IRIS SLIDE NAMESPACE

#endif /* IrisSlidePriv_h */
