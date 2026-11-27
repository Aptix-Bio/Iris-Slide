// RTM Dicom_03
// HighestInstanceOnly_22.4. Decided 2026-11-27 (ARCHITECTURE D18): from a DICOM series the encoder
// reads only the highest-resolution VOLUME instance; a TILED_FULL instance's frames are in
// row-major tile order, any other DimensionOrganizationType is refused naming it; the physical
// scale is read from Pixel Spacing unrounded (audit C12).
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlideDicomSeries.hpp"

using Catch::Matchers::ContainsSubstring;
using IrisSlide::DicomInstance;

namespace {
DicomInstance instance(std::string sop_uid, std::string flavor, std::uint64_t side,
                       std::string organization = "TILED_FULL")
{
    return DicomInstance {
        .sop_uid                = std::move(sop_uid),
        .image_flavor           = std::move(flavor),
        .dimension_organization = std::move(organization),
        .total_columns          = side,
        .total_rows             = side,
        .columns                = 256,
        .rows                   = 256,
    };
}
} // namespace

TEST_CASE("The highest-resolution VOLUME instance of a series is its source", "[Dicom_03]")
{
    // Three levels of a pyramid, smallest first, and an overview larger than every level but one.
    const std::vector<DicomInstance> series {
        instance("1.2.3.1", "VOLUME", 4096),
        instance("1.2.3.2", "OVERVIEW", 8192),
        instance("1.2.3.3", "VOLUME", 16384),
        instance("1.2.3.4", "VOLUME", 1024),
        instance("1.2.3.5", "LABEL", 512),
    };
    CHECK(IrisSlide::highest_volume(series).sop_uid == "1.2.3.3");
}

TEST_CASE("A highest instance that is not TILED_FULL is refused naming its DimensionOrganizationType",
          "[Dicom_03]")
{
    const std::vector<DicomInstance> sparse {instance("1.2.3.1", "VOLUME", 4096),
                                             instance("1.2.3.2", "VOLUME", 16384, "TILED_SPARSE")};
    CHECK_THROWS_WITH(IrisSlide::highest_volume(sparse),
                      ContainsSubstring("DimensionOrganizationType") && ContainsSubstring("TILED_SPARSE") &&
                      ContainsSubstring("1.2.3.2"));
    const std::vector<DicomInstance> absent {instance("1.2.3.9", "VOLUME", 4096, "")};
    CHECK_THROWS_WITH(IrisSlide::highest_volume(absent), ContainsSubstring("DimensionOrganizationType"));
}

TEST_CASE("Two VOLUME instances of the same size are refused naming both", "[Dicom_03]")
{
    // A tie decides nothing: two optical paths or focal planes at one size are not one source.
    const std::vector<DicomInstance> tied {instance("1.2.3.1", "VOLUME", 8192), instance("1.2.3.2", "VOLUME", 8192)};
    CHECK_THROWS_WITH(IrisSlide::highest_volume(tied), ContainsSubstring("1.2.3.1") && ContainsSubstring("1.2.3.2"));
}

TEST_CASE("A series with no VOLUME instance is refused", "[Dicom_03]")
{
    const std::vector<DicomInstance> none {instance("1.2.3.1", "LABEL", 512), instance("1.2.3.2", "OVERVIEW", 2048)};
    CHECK_THROWS_WITH(IrisSlide::highest_volume(none), ContainsSubstring("VOLUME"));
    CHECK_THROWS_AS(IrisSlide::highest_volume({}), std::runtime_error);
}

TEST_CASE("Pixel Spacing is kept to the precision it was stored with", "[Dicom_03]")
{
    // DS values are decimal text, row spacing first, in millimetres (PS3.3 10.7.1.3).
    const auto spacing = IrisSlide::parse_pixel_spacing("0.2501\\0.2502");
    CHECK(spacing.row_mm == 0.2501);
    CHECK(spacing.column_mm == 0.2502);
    const auto padded = IrisSlide::parse_pixel_spacing(" 0.000250 \\ 0.000250 ");
    CHECK(padded.row_mm == 0.000250);
    CHECK(padded.column_mm == 0.000250);
}

TEST_CASE("A Pixel Spacing that is not two positive decimals is refused naming the text", "[Dicom_03]")
{
    CHECK_THROWS_WITH(IrisSlide::parse_pixel_spacing("0.25"), ContainsSubstring("0.25"));
    CHECK_THROWS_WITH(IrisSlide::parse_pixel_spacing("0.25\\abc"), ContainsSubstring("abc"));
    CHECK_THROWS_WITH(IrisSlide::parse_pixel_spacing("0\\0.25"), ContainsSubstring("0\\0.25"));
    CHECK_THROWS_WITH(IrisSlide::parse_pixel_spacing("0.25\\0.25\\0.25"), ContainsSubstring("0.25\\0.25\\0.25"));
}
