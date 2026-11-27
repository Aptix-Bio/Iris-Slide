// RTM Dicom_04
// Jpeg2000SyntaxesMapped_22.2. Decided 2026-11-27 (ARCHITECTURE D17): each DICOM transfer syntax
// whose frames the encoder decodes maps to a source codec, a scoped enumeration of the encoder's
// own, since a source's codec is not an IFE tile encoding; any other transfer syntax is refused
// naming its UID. ClinicalColour_24.9 (decided 2026-11-27): a frame's PhotometricInterpretation
// names the components its stream stores, checked against what each codec can carry.
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlideSourceCodec.hpp"

using Catch::Matchers::ContainsSubstring;
using IrisSlide::SourceCodec;
using IrisSlide::FrameColor;

TEST_CASE("Each JPEG, JPEG 2000 and HTJ2K transfer syntax maps to its source codec", "[Dicom_04]")
{
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.50") == SourceCodec::JPEG);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.51") == SourceCodec::JPEG);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.70") == SourceCodec::JPEG);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.90") == SourceCodec::JPEG_2000);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.91") == SourceCodec::JPEG_2000);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.201") == SourceCodec::HTJ2K);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.202") == SourceCodec::HTJ2K);
    CHECK(IrisSlide::source_codec("1.2.840.10008.1.2.4.203") == SourceCodec::HTJ2K);
}

TEST_CASE("Any other transfer syntax is refused, naming its UID", "[Dicom_04]")
{
    // JPEG-LS Lossless, an encapsulated syntax the encoder has no decoder for.
    CHECK_THROWS_WITH(IrisSlide::source_codec("1.2.840.10008.1.2.4.80"),
                      ContainsSubstring("1.2.840.10008.1.2.4.80"));
    // A prefix of a mapped UID is a different UID.
    CHECK_THROWS_WITH(IrisSlide::source_codec("1.2.840.10008.1.2.4.20"),
                      ContainsSubstring("1.2.840.10008.1.2.4.20"));
    CHECK_THROWS_AS(IrisSlide::source_codec(""), std::runtime_error);
}

TEST_CASE("Each codec's PhotometricInterpretation names the components its frames store", "[Dicom_04]")
{
    CHECK(IrisSlide::frame_color(SourceCodec::JPEG, "YBR_FULL_422") == FrameColor::YCBCR);
    CHECK(IrisSlide::frame_color(SourceCodec::JPEG, "YBR_FULL") == FrameColor::YCBCR);
    CHECK(IrisSlide::frame_color(SourceCodec::JPEG, "RGB") == FrameColor::RGB);
    CHECK(IrisSlide::frame_color(SourceCodec::JPEG_2000, "YBR_RCT") == FrameColor::YCBCR);
    CHECK(IrisSlide::frame_color(SourceCodec::HTJ2K, "YBR_ICT") == FrameColor::YCBCR);
    CHECK(IrisSlide::frame_color(SourceCodec::HTJ2K, "RGB") == FrameColor::RGB);
}

TEST_CASE("A PhotometricInterpretation a codec cannot carry is refused naming both", "[Dicom_04]")
{
    // YBR_FULL in JPEG 2000 stores YCbCr with no multiple-component transform to undo it: decoded as
    // RGB its colours would be wrong.
    CHECK_THROWS_WITH(IrisSlide::frame_color(SourceCodec::HTJ2K, "YBR_FULL"),
                      ContainsSubstring("YBR_FULL") && ContainsSubstring("HTJ2K"));
    CHECK_THROWS_WITH(IrisSlide::frame_color(SourceCodec::JPEG, "MONOCHROME2"), ContainsSubstring("MONOCHROME2"));
    CHECK_THROWS_WITH(IrisSlide::frame_color(SourceCodec::JPEG, "YBR_RCT"), ContainsSubstring("JPEG"));
}
