//
//  IrisSlideSourceCodec.hpp
//  Iris
//
//  The codec of an encoder source's frames, read from its DICOM transfer syntax.
//
#ifndef IrisSlideSourceCodec_hpp
#define IrisSlideSourceCodec_hpp
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace IrisSlide {
// RTM Dicom_04
/// The codec a source's frames are compressed with. It is the encoder's own enumeration, since a
/// source's codec is not an IFE tile encoding: a JPEG 2000 or HTJ2K frame is decoded and
/// re-encoded, never passed through (ARCHITECTURE D17, decided 2026-11-27).
enum class SourceCodec : std::uint8_t {
    JPEG,       ///< ISO/IEC 10918-1: baseline, extended and lossless process 14
    JPEG_2000,  ///< ISO/IEC 15444-1, decoded by OpenJPEG
    HTJ2K,      ///< ISO/IEC 15444-15, decoded by OpenJPEG (2.5.0 and later)
};

/// The source codec of a DICOM transfer syntax UID. Throws std::runtime_error naming the UID for
/// any transfer syntax the encoder does not decode.
SourceCodec source_codec (std::string_view transfer_syntax_uid);

// MARK: - Implementation

namespace detail {
struct TransferSyntaxCodec {
    std::string_view uid;
    SourceCodec      codec;
};
/// DICOM PS3.6 Table A-1 (read 2026-11-27): the encapsulated transfer syntaxes whose frames the
/// encoder decodes, one row each.
constexpr std::array<TransferSyntaxCodec, 8> TRANSFER_SYNTAX_CODECS {{
    {"1.2.840.10008.1.2.4.50",  SourceCodec::JPEG},       // JPEG Baseline (Process 1)
    {"1.2.840.10008.1.2.4.51",  SourceCodec::JPEG},       // JPEG Extended (Process 2 & 4)
    {"1.2.840.10008.1.2.4.70",  SourceCodec::JPEG},       // JPEG Lossless, First-Order Prediction
    {"1.2.840.10008.1.2.4.90",  SourceCodec::JPEG_2000},  // JPEG 2000 (Lossless Only)
    {"1.2.840.10008.1.2.4.91",  SourceCodec::JPEG_2000},  // JPEG 2000
    {"1.2.840.10008.1.2.4.201", SourceCodec::HTJ2K},      // HTJ2K (Lossless Only)
    {"1.2.840.10008.1.2.4.202", SourceCodec::HTJ2K},      // HTJ2K with RPCL Options (Lossless Only)
    {"1.2.840.10008.1.2.4.203", SourceCodec::HTJ2K},      // HTJ2K
}};
} // END DETAIL NAMESPACE

inline SourceCodec source_codec (std::string_view transfer_syntax_uid)
{
    for (const auto& row : detail::TRANSFER_SYNTAX_CODECS)
        if (row.uid == transfer_syntax_uid) return row.codec;
    throw std::runtime_error("the encoder does not decode transfer syntax \"" +
                             std::string(transfer_syntax_uid) +
                             "\"; it decodes JPEG (1.2.840.10008.1.2.4.50, .51, .70), "
                             "JPEG 2000 (.90, .91) and HTJ2K (.201, .202, .203)");
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideSourceCodec_hpp */
