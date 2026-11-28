// RTM Encode_10, Dicom_03, Dicom_05, Dicom_06
// DicomSourceSynthetic_24.12 (decided 2026-11-27): the DICOM source is tested on whole-slide
// instances built in the test (dicom_builder.hpp), so CI covers it with no slide hosted: series
// reading, the frame index, the codec and colour checks, the re-cut, and an encode through the
// public API. ClinicalSpacing_24.11: a pixel that is not square is refused naming both spacings,
// since a slide's one microns-per-pixel value would misstate one axis.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <turbojpeg.h>

#include "IrisSlideCore.hpp"
#include "IrisSlideDicomSource.hpp"
#include "dicom_builder.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::uint32_t FRAME = 240;                      // a frame side that is not the tile's
constexpr std::uint32_t WIDTH = 300, HEIGHT = 260;        // 2 x 2 frames, 2 x 2 tiles, partly beyond
constexpr std::uint32_t FRAMES_ACROSS = 2, FRAMES_DOWN = 2;
constexpr int QUALITY = 95;
constexpr std::size_t RGB = IrisSlide::RGB_BYTES, RGBA = IrisSlide::RGBA_BYTES;
constexpr std::uint8_t WHITE = 0xFF;
constexpr double PSNR_FLOOR_DB = 36.0;                    // quality 95, 4:4:4, a smooth pattern
constexpr double MICRONS_PER_MILLIMETRE = 1000.0;

std::uint8_t pattern(std::uint64_t x, std::uint64_t y, std::size_t channel, std::uint8_t lift = 0)
{
    return static_cast<std::uint8_t>(40 + (x / 2 + y / 3 + channel * 50) % 170 + lift);
}

/// Frame (fx, fy) of the pattern, every value raised by @p lift, as a 4:4:4 JPEG stream (stored
/// YCbCr, libjpeg-turbo's default).
dicom_builder::Bytes jpeg_frame(std::uint32_t fx, std::uint32_t fy, std::uint8_t lift = 0)
{
    std::vector<std::uint8_t> rgb(std::size_t{FRAME} * FRAME * RGB);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        const std::size_t pixel = i / RGB;
        rgb[i] = pattern(std::uint64_t{fx} * FRAME + pixel % FRAME, std::uint64_t{fy} * FRAME + pixel / FRAME, i % RGB, lift);
    }
    std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_COMPRESS), &tj3Destroy);
    REQUIRE(handle);
    REQUIRE(tj3Set(handle.get(), TJPARAM_QUALITY, QUALITY) == 0);
    REQUIRE(tj3Set(handle.get(), TJPARAM_SUBSAMP, TJSAMP_444) == 0);
    unsigned char* out = nullptr;
    std::size_t size = 0;
    REQUIRE(tj3Compress8(handle.get(), rgb.data(), FRAME, 0, FRAME, TJPF_RGB, &out, &size) == 0);
    dicom_builder::Bytes jpeg(out, out + size);
    tj3Free(out);
    return jpeg;
}

dicom_builder::WholeSlide slide()
{
    dicom_builder::WholeSlide built {.width = WIDTH, .height = HEIGHT, .frame_columns = FRAME, .frame_rows = FRAME};
    for (std::uint32_t fy = 0; fy < FRAMES_DOWN; ++fy)
        for (std::uint32_t fx = 0; fx < FRAMES_ACROSS; ++fx) built.frames.push_back(jpeg_frame(fx, fy));
    return built;
}

/// PSNR of tile (tx, ty) against the pattern raised by @p lift inside the slide; every value beyond it must be white.
double tile_psnr(std::span<const std::uint8_t> tile, std::uint32_t tx, std::uint32_t ty, std::size_t& not_white,
                 std::uint8_t lift = 0)
{
    double squared = 0;
    std::size_t inside = 0;
    for (std::size_t pixel = 0; pixel < std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE; ++pixel) {
        const std::uint64_t gx = std::uint64_t{tx} * IrisSlide::RECUT_TILE + pixel % IrisSlide::RECUT_TILE;
        const std::uint64_t gy = std::uint64_t{ty} * IrisSlide::RECUT_TILE + pixel / IrisSlide::RECUT_TILE;
        const bool is_inside = gx < WIDTH && gy < HEIGHT;
        for (std::size_t c = 0; c < RGB; ++c) {
            const std::uint8_t value = tile[pixel * RGBA + c];
            if (!is_inside) { not_white += value != WHITE; continue; }
            squared += (double(value) - pattern(gx, gy, c, lift)) * (double(value) - pattern(gx, gy, c, lift));
            ++inside;
        }
    }
    return 10 * std::log10(255.0 * 255.0 / (squared / double(inside)));
}
} // namespace

// RTM Encode_13
TEST_CASE("A Z-stacked instance is read as every focal plane of each tile, in Z order", "[Encode_10][Encode_13]")
{
    // ZStackSource_27.9: three planes, each the pattern raised by its own amount, so a plane read
    // from the wrong frames is told apart.
    constexpr std::uint32_t PLANES = 3;
    constexpr std::uint8_t LIFT_STEP = 20;
    dicom_builder::WholeSlide built {.width = WIDTH, .height = HEIGHT, .frame_columns = FRAME, .frame_rows = FRAME};
    built.focal_planes = PLANES;
    built.plane_spacing_mm = 0.001f;
    for (std::uint32_t p = 0; p < PLANES; ++p)
        for (std::uint32_t fy = 0; fy < FRAMES_DOWN; ++fy)
            for (std::uint32_t fx = 0; fx < FRAMES_ACROSS; ++fx)
                built.frames.push_back(jpeg_frame(fx, fy, static_cast<std::uint8_t>(p * LIFT_STEP)));
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(built), "source_zstack");
    auto source = IrisSlide::open_dicom_source(path);
    REQUIRE(source->planes() == PLANES);
    CHECK(std::abs(source->measurements().microns_between_planes - 1.0) < 1e-6);
    constexpr std::size_t TILE_BYTES = std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA;
    std::vector<std::uint8_t> planes(PLANES * TILE_BYTES);
    source->read_tile(1, 1, planes);
    for (std::uint32_t p = 0; p < PLANES; ++p) {
        INFO("plane " << p);
        std::size_t not_white = 0;
        CHECK(tile_psnr(std::span(planes).subspan(p * TILE_BYTES, TILE_BYTES), 1, 1, not_white,
                        static_cast<std::uint8_t>(p * LIFT_STEP)) > PSNR_FLOOR_DB);
        CHECK(not_white == 0);
    }
}

// RTM Encode_13
TEST_CASE("An instance declaring 0 focal planes, or a Z-stack whose planes are not a positive distance apart, is refused naming it",
          "[Encode_10][Encode_13]")
{
    dicom_builder::WholeSlide none = slide();
    none.focal_planes = 0;
    const auto none_path = dicom_builder::write_instance(dicom_builder::whole_slide(none), "source_no_planes");
    CHECK_THROWS_WITH(IrisSlide::open_dicom_source(none_path), ContainsSubstring("TotalPixelMatrixFocalPlanes") && ContainsSubstring("is 0"));
    dicom_builder::WholeSlide backward {.width = WIDTH, .height = HEIGHT, .frame_columns = FRAME, .frame_rows = FRAME};
    backward.focal_planes = 2;
    backward.plane_spacing_mm = -0.001f;
    for (std::uint32_t p = 0; p < backward.focal_planes; ++p)
        for (std::uint32_t fy = 0; fy < FRAMES_DOWN; ++fy)
            for (std::uint32_t fx = 0; fx < FRAMES_ACROSS; ++fx) backward.frames.push_back(jpeg_frame(fx, fy));
    const auto backward_path = dicom_builder::write_instance(dicom_builder::whole_slide(backward), "source_negative_spacing");
    CHECK_THROWS_WITH(IrisSlide::open_dicom_source(backward_path), ContainsSubstring("DistanceBetweenFocalPlanes") && ContainsSubstring("-0.001"));
}

TEST_CASE("A synthetic JPEG whole-slide instance is read as 256-pixel tiles from its 240-pixel frames",
          "[Encode_10][Dicom_05]")
{
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(slide()), "source_tiles");
    auto source = IrisSlide::open_dicom_source(path);
    CHECK(source->size().width == WIDTH);
    CHECK(source->size().height == HEIGHT);
    CHECK(source->measurements().microns_per_pixel == 0.00025 * MICRONS_PER_MILLIMETRE);
    std::vector<std::uint8_t> tile(std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA);
    std::size_t not_white = 0;
    for (std::uint32_t ty = 0; ty < 2; ++ty)
        for (std::uint32_t tx = 0; tx < 2; ++tx) {
            source->read_tile(tx, ty, tile);
            const double psnr = tile_psnr(tile, tx, ty, not_white);
            INFO("tile " << tx << ", " << ty << ": PSNR " << psnr << " dB");
            CHECK(psnr >= PSNR_FLOOR_DB);
        }
    CHECK(not_white == 0);
    source.reset();   // the source maps the file, and Windows deletes no mapped file
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("A pixel that is not square is refused naming both spacings", "[Dicom_03]")
{
    auto built = slide();
    built.pixel_spacing = "0.00025\\0.0005";
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(built), "source_spacing");
    CHECK_THROWS_WITH(IrisSlide::open_dicom_source(path), ContainsSubstring("0.00025") && ContainsSubstring("0.0005"));
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Frames whose colour disagrees with the instance's PhotometricInterpretation are refused", "[Dicom_05]")
{
    auto built = slide();
    built.photometric = "RGB";   // the frames store YCbCr
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(built), "source_colour");
    auto source = IrisSlide::open_dicom_source(path);
    std::vector<std::uint8_t> tile(std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA);
    CHECK_THROWS_WITH(source->read_tile(0, 0, tile), ContainsSubstring("YCbCr") && ContainsSubstring("RGB"));
    source.reset();   // the source maps the file, and Windows deletes no mapped file
    built.photometric = "MONOCHROME2";
    const auto mono = dicom_builder::write_instance(dicom_builder::whole_slide(built), "source_mono");
    CHECK_THROWS_WITH(IrisSlide::open_dicom_source(mono), ContainsSubstring("MONOCHROME2"));
    std::filesystem::remove_all(path.parent_path());
    std::filesystem::remove_all(mono.parent_path());
}

TEST_CASE("A synthetic JPEG whole-slide instance encodes through the public API", "[Encode_09][Encode_10]")
{
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(slide()), "source_encode");
    IrisSlide::Encoder encoder;
    const IrisSlide::EncoderDesc desc {.source = path, .destination = path.parent_path()};
    const auto created = IrisSlide::create_encoder(desc, encoder);
    INFO(created.message);
    REQUIRE(created.succeeded());
    REQUIRE(IrisSlide::dispatch_encoder(encoder).succeeded());
    IrisSlide::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        REQUIRE(IrisSlide::encoder_progress(encoder, progress).succeeded());
    } while (progress.state == IrisSlide::EncoderState::ACTIVE);
    INFO(progress.error);
    CHECK(progress.state == IrisSlide::EncoderState::DONE);
    CHECK(progress.file == path.parent_path() / "instance.jpeg.iris");
    CHECK(std::filesystem::file_size(progress.file) > 0);
    encoder.reset();
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("An instance the encoder does not read may lack Pixel Spacing; the one it encodes may not", "[Dicom_03]")
{
    // A series' LABEL image often has no SharedFunctionalGroupsSequence (seen in a published JPEG
    // 2000 series): it must not make the series unreadable, since only the VOLUME is encoded.
    dicom_builder::WholeSlide label {.width = 240, .height = 240, .frame_columns = 240, .frame_rows = 240};
    label.image_flavor = "LABEL";
    label.pixel_spacing.clear();
    label.sop_uid = "1.2.826.0.1.3680043.10.5";
    label.frames.push_back(jpeg_frame(0, 0));
    const auto series = dicom_builder::write_series({dicom_builder::whole_slide(slide()), dicom_builder::whole_slide(label)},
                                                    "source_label");
    auto source = IrisSlide::open_dicom_source(series[1]);   // named by its label: the VOLUME is chosen
    CHECK(source->size().width == WIDTH);
    source.reset();   // the source maps the file, and Windows deletes no mapped file
    auto unmeasured = slide();
    unmeasured.pixel_spacing.clear();
    const auto path = dicom_builder::write_instance(dicom_builder::whole_slide(unmeasured), "source_unmeasured");
    CHECK_THROWS_WITH(IrisSlide::open_dicom_source(path), ContainsSubstring("Pixel Spacing"));
    std::filesystem::remove_all(series[0].parent_path());
    std::filesystem::remove_all(path.parent_path());
}
