//
//  IrisSlideEncoderOptions.hpp
//  Iris
//
//  The encoder's options as a caller types them, read once for the command line and the Python
//  module, so the two accept the same text and refuse it with the same words.
//
#ifndef IrisSlideEncoderOptions_hpp
#define IrisSlideEncoderOptions_hpp

#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "IrisSlideTypes.hpp"

namespace IrisSlide {
// RTM Encode_09, Bindings_01
/// A chroma subsampling and the name callers type for it.
struct SubsamplingName {
    std::string_view name;
    Subsampling      subsampling;
};
/// Every subsampling the encoder writes, by name. Each row states its enumerator, so the table
/// holds whatever numbering Iris-Headers gives them.
constexpr std::array<SubsamplingName, 3> SUBSAMPLING_NAMES {{
    {"444", SUBSAMPLE_444},
    {"422", SUBSAMPLE_422},
    {"420", SUBSAMPLE_420},
}};

/// The subsampling @p text names; throws std::invalid_argument naming the text otherwise.
inline Subsampling subsampling_of (std::string_view text)
{
    for (const auto& row : SUBSAMPLING_NAMES)
        if (row.name == text) return row.subsampling;
    throw std::invalid_argument("subsampling \"" + std::string(text) + "\" is not 444, 422 or 420");
}

/// The name a caller types for @p subsampling; throws std::invalid_argument for one with no row.
inline std::string_view subsampling_name (Subsampling subsampling)
{
    for (const auto& row : SUBSAMPLING_NAMES)
        if (row.subsampling == subsampling) return row.name;
    throw std::invalid_argument("subsampling " + std::to_string(int(subsampling)) + " has no name");
}

/// @p value as T, refused naming @p what when T cannot hold it, so a large number is never
/// wrapped into one the encoder accepts (65626 as a 16-bit quality is 90). It throws
/// std::invalid_argument, which Python's binding raises as ValueError; pybind11 would raise
/// std::out_of_range as IndexError, the error for a sequence index.
template <std::unsigned_integral T>
T narrowed (std::uint64_t value, std::string_view what)
{
    if (value > std::numeric_limits<T>::max())
        throw std::invalid_argument(std::string(what) + " " + std::to_string(value) + " is above " +
                                std::to_string(std::numeric_limits<T>::max()));
    return static_cast<T>(value);
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideEncoderOptions_hpp */
