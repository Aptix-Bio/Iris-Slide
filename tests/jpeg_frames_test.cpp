// RTM Dicom_05
// JpegFrames_22.9 (decided 2026-11-27): a DICOM instance whose frames are JPEG (1.2.840.10008.1.2.4.50,
// .51, .70) is a source like JPEG 2000 and HTJ2K ones: each frame decodes through libjpeg-turbo into a
// buffer the caller passes, sized from the stream's own header and compared with the instance's
// Rows and Columns (D3), so the encoder re-cuts and re-encodes it.
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <turbojpeg.h>

#include "IrisSlideFrameCodec.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr int SIDE = 240;
constexpr int QUALITY = 95;
constexpr double PSNR_FLOOR_DB = 38.0;   // quality 95, 4:4:4 on a smooth pattern

std::vector<std::uint8_t> pattern()
{
    std::vector<std::uint8_t> rgb(std::size_t{SIDE} * SIDE * 3);
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x) {
            auto* px = &rgb[(std::size_t(y) * SIDE + x) * 3];
            px[0] = std::uint8_t(60 + x / 2); px[1] = std::uint8_t(40 + y / 2); px[2] = std::uint8_t(120 + (x + y) / 8);
        }
    return rgb;
}

std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& rgb)
{
    std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_COMPRESS), &tj3Destroy);
    REQUIRE(handle);
    REQUIRE(tj3Set(handle.get(), TJPARAM_QUALITY, QUALITY) == 0);
    REQUIRE(tj3Set(handle.get(), TJPARAM_SUBSAMP, TJSAMP_444) == 0);
    unsigned char* out = nullptr;
    std::size_t size = 0;
    REQUIRE(tj3Compress8(handle.get(), rgb.data(), SIDE, 0, SIDE, TJPF_RGB, &out, &size) == 0);
    std::vector<std::uint8_t> jpeg(out, out + size);
    tj3Free(out);
    return jpeg;
}
} // namespace

TEST_CASE("A JPEG frame decodes to the pixels it was made from, within JPEG's loss", "[Dicom_05]")
{
    const auto rgb = pattern();
    const auto jpeg = compress(rgb);
    std::vector<std::uint8_t> decoded(rgb.size());
    IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, jpeg, {.columns = SIDE, .rows = SIDE}, decoded);
    double squared = 0;
    for (std::size_t i = 0; i < rgb.size(); ++i) squared += (double(rgb[i]) - decoded[i]) * (double(rgb[i]) - decoded[i]);
    const double psnr = 10 * std::log10(255.0 * 255.0 / (squared / double(rgb.size())));
    INFO("PSNR " << psnr << " dB");
    CHECK(psnr >= PSNR_FLOOR_DB);
    CHECK(IrisSlide::is_decodable(IrisSlide::SourceCodec::JPEG));
}

TEST_CASE("A JPEG frame whose header disagrees with the instance is refused naming both sizes", "[Dicom_05]")
{
    const auto jpeg = compress(pattern());
    std::vector<std::uint8_t> decoded(std::size_t{256} * 256 * 3);
    CHECK_THROWS_WITH(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, jpeg, {.columns = 256, .rows = 256}, decoded),
                      ContainsSubstring("240 x 240") && ContainsSubstring("256 x 256"));
}

TEST_CASE("A truncated JPEG frame is refused", "[Dicom_05]")
{
    auto jpeg = compress(pattern());
    jpeg.resize(jpeg.size() / 2);
    std::vector<std::uint8_t> decoded(std::size_t{SIDE} * SIDE * 3);
    CHECK_THROWS_AS(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, jpeg, {.columns = SIDE, .rows = SIDE}, decoded),
                    std::runtime_error);
}
