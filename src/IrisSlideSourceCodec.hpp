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
/// source's codec is not an IFE tile encoding: IFE has no JPEG 2000 or HTJ2K tile encoding, so the
/// rebuilt encoder decodes such a frame (decode_jpeg2000_frame) and compresses its pixels again
/// (ARCHITECTURE D17, decided 2026-11-27).
enum class SourceCodec : std::uint8_t {
    JPEG,       ///< ISO/IEC 10918-1: baseline, extended and lossless process 14
    JPEG_2000,  ///< ISO/IEC 15444-1, decoded by OpenJPEG
    HTJ2K,      ///< ISO/IEC 15444-15, decoded by OpenJPEG (2.5.0 and later)
};

/// The source codec of a DICOM transfer syntax UID. Throws std::runtime_error naming the UID for
/// any transfer syntax the encoder does not decode.
SourceCodec source_codec (std::string_view transfer_syntax_uid);

// RTM Dicom_04
/// The components a frame's stream stores, before its codec's colour transform: RGB, or YCbCr that
/// the decoder converts to RGB (JPEG's colour conversion; JPEG 2000's multiple-component transform).
/// Each decoder checks its stream says the same, so a mislabelled frame is refused instead of
/// decoded into wrong colours.
enum class FrameColor : std::uint8_t {
    RGB,
    YCBCR,
};

/// The frame colour @p photometric (PhotometricInterpretation, PS3.3 C.7.6.3.1.2) declares for
/// @p codec (PS3.5 8.2.1, 8.2.4, A.4.4). Throws std::runtime_error naming both for a value the codec
/// cannot carry as 8-bit colour: YBR_FULL in JPEG 2000 (YCbCr with no transform to undo it), YBR_ICT
/// or YBR_RCT in JPEG, and anything monochrome or palette.
FrameColor frame_color (SourceCodec codec, std::string_view photometric);

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
struct PhotometricColor {
    std::string_view photometric;
    bool             is_jpeg;   // a JPEG value when true, a JPEG 2000 and HTJ2K one when false
    FrameColor       color;
};
/// PS3.5 8.2.1 (JPEG) and 8.2.4 / A.4.4 (JPEG 2000, HTJ2K), read 2026-11-27.
constexpr std::array<PhotometricColor, 6> PHOTOMETRIC_COLORS {{
    {"YBR_FULL_422", true,  FrameColor::YCBCR},
    {"YBR_FULL",     true,  FrameColor::YCBCR},
    {"RGB",          true,  FrameColor::RGB},
    {"YBR_ICT",      false, FrameColor::YCBCR},   // irreversible colour transform
    {"YBR_RCT",      false, FrameColor::YCBCR},   // reversible colour transform
    {"RGB",          false, FrameColor::RGB},     // no colour transform
}};
constexpr std::string_view codec_name (SourceCodec codec)
{
    switch (codec) {
        case SourceCodec::JPEG:      return "JPEG";
        case SourceCodec::JPEG_2000: return "JPEG 2000";
        case SourceCodec::HTJ2K:     return "HTJ2K";
    }
    return "an unknown codec";
}
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

inline FrameColor frame_color (SourceCodec codec, std::string_view photometric)
{
    const bool is_jpeg = codec == SourceCodec::JPEG;
    for (const auto& row : detail::PHOTOMETRIC_COLORS)
        if (row.photometric == photometric && row.is_jpeg == is_jpeg) return row.color;
    throw std::runtime_error("PhotometricInterpretation \"" + std::string(photometric) + "\" is not one " +
                             std::string(detail::codec_name(codec)) + " frames carry as 8-bit colour (" +
                             (is_jpeg ? "YBR_FULL_422, YBR_FULL or RGB" : "YBR_ICT, YBR_RCT or RGB") + ")");
}
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideSourceCodec_hpp */
