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
#include "IrisSlideJpegFrames.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr int SIDE = 240;
constexpr int OTHER_SIDE = 256;          // a size the instance may declare that the stream is not
constexpr int QUALITY = 95;
constexpr std::size_t RGB = 3, RGBA = 4;
constexpr std::uint8_t OPAQUE = 0xFF;
constexpr double PSNR_FLOOR_DB = 38.0;   // quality 95, 4:4:4 on a smooth pattern

std::vector<std::uint8_t> pattern()
{
    std::vector<std::uint8_t> rgb(std::size_t{SIDE} * SIDE * RGB);
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x) {
            auto* px = &rgb[(std::size_t(y) * SIDE + x) * RGB];
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
    IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::YCBCR, jpeg, {.columns = SIDE, .rows = SIDE}, decoded);
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
    std::vector<std::uint8_t> decoded(std::size_t{OTHER_SIDE} * OTHER_SIDE * RGB);
    CHECK_THROWS_WITH(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::YCBCR, jpeg, {.columns = OTHER_SIDE, .rows = OTHER_SIDE}, decoded),
                      ContainsSubstring("240 x 240") && ContainsSubstring("256 x 256"));
}

TEST_CASE("A truncated JPEG frame is refused", "[Dicom_05]")
{
    auto jpeg = compress(pattern());
    jpeg.resize(jpeg.size() / 2);
    std::vector<std::uint8_t> decoded(std::size_t{SIDE} * SIDE * RGB);
    CHECK_THROWS_AS(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::YCBCR, jpeg, {.columns = SIDE, .rows = SIDE}, decoded),
                    std::runtime_error);
}

TEST_CASE("A JPEG frame of 12-bit samples is refused naming its precision", "[Dicom_05]")
{
    // libjpeg-turbo 3 writes 12-bit JPEG from 16-bit samples (tj3Compress12).
    std::vector<std::int16_t> samples(std::size_t{SIDE} * SIDE * RGB, 2048);
    std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_COMPRESS), &tj3Destroy);
    REQUIRE(handle);
    REQUIRE(tj3Set(handle.get(), TJPARAM_QUALITY, QUALITY) == 0);
    REQUIRE(tj3Set(handle.get(), TJPARAM_SUBSAMP, TJSAMP_444) == 0);
    unsigned char* out = nullptr;
    std::size_t size = 0;
    REQUIRE(tj3Compress12(handle.get(), samples.data(), SIDE, 0, SIDE, TJPF_RGB, &out, &size) == 0);
    const std::vector<std::uint8_t> jpeg(out, out + size);
    tj3Free(out);
    std::vector<std::uint8_t> decoded(std::size_t{SIDE} * SIDE * RGB);
    CHECK_THROWS_WITH(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::YCBCR, jpeg, {.columns = SIDE, .rows = SIDE}, decoded),
                      ContainsSubstring("12-bit"));
}

TEST_CASE("A JPEG frame refuses a destination of the wrong size naming both byte counts", "[Dicom_05]")
{
    const auto jpeg = compress(pattern());
    std::vector<std::uint8_t> decoded(std::size_t{SIDE} * SIDE * RGB - 1);
    CHECK_THROWS_WITH(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::YCBCR, jpeg, {.columns = SIDE, .rows = SIDE}, decoded),
                      ContainsSubstring(std::to_string(decoded.size())) &&
                      ContainsSubstring(std::to_string(std::size_t{SIDE} * SIDE * RGB)));
}

TEST_CASE("A JPEG stream decodes into each 8-bit layout the reader offers, RGBA opaque", "[Dicom_05]")
{
    const auto rgb = pattern();
    const auto jpeg = compress(rgb);
    std::vector<std::uint8_t> as_rgb(rgb.size()), as_rgba(std::size_t{SIDE} * SIDE * RGBA);
    IrisSlide::decode_jpeg(jpeg, {.columns = SIDE, .rows = SIDE}, Iris::FORMAT_R8G8B8, as_rgb);
    IrisSlide::decode_jpeg(jpeg, {.columns = SIDE, .rows = SIDE}, Iris::FORMAT_R8G8B8A8, as_rgba);
    std::size_t wrong = 0;
    for (std::size_t pixel = 0; pixel < std::size_t{SIDE} * SIDE; ++pixel) {
        for (std::size_t c = 0; c < RGB; ++c) wrong += as_rgba[pixel * RGBA + c] != as_rgb[pixel * RGB + c];
        wrong += as_rgba[pixel * RGBA + RGB] != OPAQUE;
    }
    CHECK(wrong == 0);
    CHECK_THROWS_WITH(IrisSlide::decode_jpeg(jpeg, {.columns = SIDE, .rows = SIDE}, Iris::FORMAT_UNDEFINED, as_rgb),
                      ContainsSubstring("format 0"));
}

TEST_CASE("A JPEG frame whose stored colour disagrees with its PhotometricInterpretation is refused naming both",
          "[Dicom_05]")
{
    // libjpeg-turbo stores an RGB input as YCbCr by default: the instance would have to say YBR_FULL_422.
    const auto jpeg = compress(pattern());
    std::vector<std::uint8_t> decoded(std::size_t{SIDE} * SIDE * RGB);
    CHECK_THROWS_WITH(IrisSlide::decode_frame(IrisSlide::SourceCodec::JPEG, IrisSlide::FrameColor::RGB, jpeg,
                                              {.columns = SIDE, .rows = SIDE}, decoded),
                      ContainsSubstring("YCbCr") && ContainsSubstring("RGB"));
}
