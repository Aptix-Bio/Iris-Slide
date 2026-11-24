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
#define IRIS_INCLUDE_OPENSLIDE 1
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

#include <source_location>
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
} // END IRIS SLIDE NAMESPACE

#endif /* IrisSlidePriv_h */
