// RTM Dicom_05
// OpenJpegFrames_22.3. Decided 2026-11-27 (ARCHITECTURE D17): a JPEG 2000 or HTJ2K frame decodes
// through OpenJPEG into a buffer the caller passes, sized from the codestream's own header; a
// frame whose header disagrees with the instance's Rows or Columns is refused naming both, and a
// truncated codestream is refused. The codestreams are written here by OpenJPEG's encoder from a
// seeded pattern, so no slide file is read.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <openjpeg.h>

#include "IrisSlideOpenJpeg.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::uint32_t FRAME_SIDE = 240;  // the maintainer's lossless slide's frame size
constexpr std::uint32_t RGB_CHANNELS = 3;
constexpr std::uint32_t PATTERN_SEED = 20261127;
constexpr int RESOLUTIONS = 4;             // 240 px down to 30 px

/// An RGB8 frame of seeded noise over a gradient: lossless coding must reproduce every byte.
std::vector<std::uint8_t> pattern_frame()
{
    std::mt19937 random(PATTERN_SEED);
    std::uniform_int_distribution<int> noise(0, 15);
    std::vector<std::uint8_t> rgb(std::size_t{FRAME_SIDE} * FRAME_SIDE * RGB_CHANNELS);
    for (std::uint32_t y = 0; y < FRAME_SIDE; ++y)
        for (std::uint32_t x = 0; x < FRAME_SIDE; ++x)
            for (std::uint32_t c = 0; c < RGB_CHANNELS; ++c)
                rgb[(std::size_t{y} * FRAME_SIDE + x) * RGB_CHANNELS + c] =
                    static_cast<std::uint8_t>((x + y + c * 64U) % 240U + static_cast<unsigned>(noise(random)));
    return rgb;
}

struct WriteTarget { std::vector<std::uint8_t> bytes; };
OPJ_SIZE_T write_bytes(void* buffer, OPJ_SIZE_T count, void* user)
{
    auto& target = *static_cast<WriteTarget*>(user);
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    target.bytes.insert(target.bytes.end(), bytes, bytes + count);
    return count;
}
OPJ_OFF_T skip_bytes(OPJ_OFF_T count, void* user)
{
    auto& target = *static_cast<WriteTarget*>(user);
    target.bytes.resize(target.bytes.size() + static_cast<std::size_t>(count));
    return count;
}
OPJ_BOOL seek_bytes(OPJ_OFF_T offset, void* user)
{
    auto& target = *static_cast<WriteTarget*>(user);
    if (static_cast<std::size_t>(offset) > target.bytes.size())
        target.bytes.resize(static_cast<std::size_t>(offset));
    return OPJ_TRUE;
}

/// A lossless (reversible 5/3, with the reversible colour transform) J2K codestream of @p rgb.
std::vector<std::uint8_t> encode_lossless(const std::vector<std::uint8_t>& rgb)
{
    std::array<opj_image_cmptparm_t, RGB_CHANNELS> parameters {};
    for (auto& component : parameters) {
        component.dx = component.dy = 1;
        component.w = component.h = FRAME_SIDE;
        component.prec = 8;
        component.sgnd = 0;
    }
    std::unique_ptr<opj_image_t, decltype(&opj_image_destroy)> image(
        opj_image_create(RGB_CHANNELS, parameters.data(), OPJ_CLRSPC_SRGB), &opj_image_destroy);
    REQUIRE(image);
    image->x1 = image->y1 = FRAME_SIDE;
    for (std::size_t i = 0; i < std::size_t{FRAME_SIDE} * FRAME_SIDE; ++i)
        for (std::uint32_t c = 0; c < RGB_CHANNELS; ++c)
            image->comps[c].data[i] = rgb[i * RGB_CHANNELS + c];

    opj_cparameters_t settings;
    opj_set_default_encoder_parameters(&settings);
    settings.irreversible = 0;
    settings.tcp_mct = 1;
    settings.numresolution = RESOLUTIONS;
    std::unique_ptr<opj_codec_t, decltype(&opj_destroy_codec)> codec(opj_create_compress(OPJ_CODEC_J2K), &opj_destroy_codec);
    REQUIRE(opj_setup_encoder(codec.get(), &settings, image.get()));

    WriteTarget target;
    std::unique_ptr<opj_stream_t, decltype(&opj_stream_destroy)> stream(opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE),
                                                                        &opj_stream_destroy);
    opj_stream_set_user_data(stream.get(), &target, nullptr);
    opj_stream_set_write_function(stream.get(), write_bytes);
    opj_stream_set_skip_function(stream.get(), skip_bytes);
    opj_stream_set_seek_function(stream.get(), seek_bytes);
    REQUIRE(opj_start_compress(codec.get(), image.get(), stream.get()));
    REQUIRE(opj_encode(codec.get(), stream.get()));
    REQUIRE(opj_end_compress(codec.get(), stream.get()));
    return target.bytes;
}
} // namespace

TEST_CASE("A lossless JPEG 2000 frame decodes to the pixels it was made from", "[Dicom_05]")
{
    const auto rgb = pattern_frame();
    const auto codestream = encode_lossless(rgb);
    std::vector<std::uint8_t> decoded(rgb.size());
    IrisSlide::decode_jpeg2000_frame(codestream, IrisSlide::FrameColor::YCBCR, {.columns = FRAME_SIDE, .rows = FRAME_SIDE}, decoded);
    CHECK(decoded == rgb);
}

TEST_CASE("A frame whose header disagrees with the instance's Rows and Columns is refused naming both", "[Dicom_05]")
{
    const auto codestream = encode_lossless(pattern_frame());
    std::vector<std::uint8_t> decoded(std::size_t{256} * 256 * RGB_CHANNELS);
    CHECK_THROWS_WITH(IrisSlide::decode_jpeg2000_frame(codestream, IrisSlide::FrameColor::YCBCR, {.columns = 256, .rows = 256}, decoded),
                      ContainsSubstring("240 x 240") && ContainsSubstring("256 x 256"));
}

TEST_CASE("A destination of the wrong size is refused naming both sizes", "[Dicom_05]")
{
    const auto codestream = encode_lossless(pattern_frame());
    std::vector<std::uint8_t> decoded(100);
    CHECK_THROWS_WITH(IrisSlide::decode_jpeg2000_frame(codestream, IrisSlide::FrameColor::YCBCR, {.columns = FRAME_SIDE, .rows = FRAME_SIDE}, decoded),
                      ContainsSubstring("100") && ContainsSubstring("172800"));
}

TEST_CASE("A truncated codestream is refused", "[Dicom_05]")
{
    auto codestream = encode_lossless(pattern_frame());
    codestream.resize(codestream.size() / 2);
    std::vector<std::uint8_t> decoded(std::size_t{FRAME_SIDE} * FRAME_SIDE * RGB_CHANNELS);
    CHECK_THROWS_AS(IrisSlide::decode_jpeg2000_frame(codestream, IrisSlide::FrameColor::YCBCR, {.columns = FRAME_SIDE, .rows = FRAME_SIDE}, decoded),
                    std::runtime_error);
}

TEST_CASE("Bytes that are not a codestream are refused", "[Dicom_05]")
{
    const std::vector<std::uint8_t> garbage(64, 0xAB);
    std::vector<std::uint8_t> decoded(std::size_t{FRAME_SIDE} * FRAME_SIDE * RGB_CHANNELS);
    CHECK_THROWS_AS(IrisSlide::decode_jpeg2000_frame(garbage, IrisSlide::FrameColor::YCBCR, {.columns = FRAME_SIDE, .rows = FRAME_SIDE}, decoded),
                    std::runtime_error);
}

TEST_CASE("A codestream whose colour transform disagrees with its PhotometricInterpretation is refused naming both",
          "[Dicom_05]")
{
    // encode_lossless applies the reversible colour transform: the instance must say YBR_RCT, and
    // "RGB" (no transform) is refused instead of decoded as if it matched.
    const auto codestream = encode_lossless(pattern_frame());
    std::vector<std::uint8_t> decoded(std::size_t{FRAME_SIDE} * FRAME_SIDE * RGB_CHANNELS);
    CHECK_THROWS_WITH(IrisSlide::decode_jpeg2000_frame(codestream, IrisSlide::FrameColor::RGB,
                                                       {.columns = FRAME_SIDE, .rows = FRAME_SIDE}, decoded),
                      ContainsSubstring("colour transform") && ContainsSubstring("RGB"));
}
