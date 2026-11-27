// RTM Testing_01, Dicom_05, Dicom_03, Dicom_04, Dicom_06, Encode_10
// LocalSlideInputs_22.6 and OpenJpegFrames_22.3. Decided 2026-11-27: while the encoder is
// developed, the tests that need a real slide read the maintainer's own slides from the folder the
// CMake cache variable IRIS_SLIDE_TEST_SLIDES names; no slide file enters the repository. An
// unset variable, or a folder with no JPEG 2000 or HTJ2K instance, fails by name.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <thread>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;

extern "C" {
#include <dicom/dicom.h>
}

#include "IrisSlidePriv.hpp"
#include "IrisSlideDicomSeries.hpp"
#include "IrisSlideDicomFrames.hpp"
#include "IrisSlideDicomSource.hpp"
#include "IrisSlideEncoder.hpp"
#include "IrisSlideRetile.hpp"
#include "IrisSlideOpenJpeg.hpp"
#include "IrisSlideSourceCodec.hpp"

namespace {
constexpr std::size_t RGB_CHANNELS = IrisSlide::RGB_BYTES;
constexpr std::uint32_t SAMPLED_FRAMES = 16;   // spread over the instance, first and last included
constexpr std::uint32_t TAG_SOP_INSTANCE_UID = 0x00080018;
constexpr std::uint32_t TAG_PHOTOMETRIC_INTERPRETATION = 0x00280004;
constexpr std::uint32_t TAG_NUMBER_OF_FRAMES = 0x00280008;
constexpr std::uint32_t TAG_ROWS = 0x00280010;
constexpr std::uint32_t TAG_COLUMNS = 0x00280011;
constexpr char JPEG_2000_LOSSLESS[] = "1.2.840.10008.1.2.4.90";

std::filesystem::path slide_folder()
{
#ifdef IRIS_SLIDE_TEST_SLIDES
    const std::filesystem::path folder = IRIS_SLIDE_TEST_SLIDES;
    if (!std::filesystem::is_directory(folder))
        FAIL("IRIS_SLIDE_TEST_SLIDES names " << folder << ", which is not a folder");
    return folder;
#else
    FAIL("IRIS_SLIDE_TEST_SLIDES is unset: configure with -DIRIS_SLIDE_TEST_SLIDES=<folder of DICOM slides>");
    return {};
#endif
}

using Handle = std::unique_ptr<DcmFilehandle, decltype(&dcm_filehandle_destroy)>;
using Frame  = std::unique_ptr<DcmFrame, decltype(&dcm_frame_destroy)>;

[[noreturn]] void refuse_tag(std::uint32_t tag, DcmError*& error)
{
    const std::string message = error && dcm_error_get_message(error) ? dcm_error_get_message(error) : "absent";
    dcm_error_clear(&error);
    throw std::runtime_error("tag " + std::to_string(tag) + " unreadable: " + message);
}
std::string text_of(const DcmDataSet* data, std::uint32_t tag)
{
    const char* value = nullptr;
    DcmError* error = nullptr;
    DcmElement* element = dcm_dataset_get(&error, data, tag);
    if (!element || !dcm_element_get_value_string(&error, element, 0, &value) || !value)
        refuse_tag(tag, error);
    return value;
}
std::uint32_t integer_of(const DcmDataSet* data, std::uint32_t tag)
{
    int64_t value = 0;
    DcmError* error = nullptr;
    DcmElement* element = dcm_dataset_get(&error, data, tag);
    if (!element || !dcm_element_get_value_integer(&error, element, 0, &value))
        refuse_tag(tag, error);
    return static_cast<std::uint32_t>(value);
}

/// One JPEG 2000 or HTJ2K instance in the folder, open for frame reads.
struct Instance {
    std::filesystem::path path;
    std::string           syntax;
    std::string           sop_uid;
    IrisSlide::FrameShape shape;
    IrisSlide::FrameColor color = IrisSlide::FrameColor::RGB;
    std::uint32_t         frames = 0;
    Handle                handle {nullptr, &dcm_filehandle_destroy};

    std::vector<std::uint8_t> decode(std::uint32_t frame_number) const
    {
        DcmError* error = nullptr;
        Frame frame(dcm_filehandle_read_frame(&error, handle.get(), frame_number), &dcm_frame_destroy);
        if (!frame) {
            const std::string message = error ? dcm_error_get_message(error) : "";
            dcm_error_clear(&error);
            throw std::runtime_error(path.filename().string() + " frame " + std::to_string(frame_number) + ": " + message);
        }
        const auto* bytes = static_cast<const std::uint8_t*>(static_cast<const void*>(dcm_frame_get_value(frame.get())));
        std::vector<std::uint8_t> rgb(std::size_t{shape.columns} * shape.rows * RGB_CHANNELS);
        IrisSlide::decode_jpeg2000_frame(std::span(bytes, dcm_frame_get_length(frame.get())), color, shape, rgb);
        return rgb;
    }
};

/// Every .dcm under the folder whose transfer syntax is JPEG 2000 or HTJ2K.
std::vector<Instance> jpeg2000_instances(const std::filesystem::path& folder)
{
    std::vector<Instance> found;
    // Hidden folders (a Python environment's sample data, an editor's settings) hold no slides.
    for (auto entry = std::filesystem::recursive_directory_iterator(folder);
         entry != std::filesystem::recursive_directory_iterator(); ++entry) {
        if (entry->is_directory() && entry->path().filename().string().starts_with(".")) {
            entry.disable_recursion_pending();
            continue;
        }
        if (!entry->is_regular_file() || entry->path().extension() != ".dcm") continue;
        Instance instance {.path = entry->path()};
        // A .dcm that does not open, or has no metadata or transfer syntax, fails the test naming
        // its file, since the test claims every slide in the folder.
        DcmError* error = nullptr;
        instance.handle.reset(dcm_filehandle_create_from_file(&error, entry->path().string().c_str()));
        // libdicom reads the transfer syntax with the metadata, so the metadata is read first.
        const DcmDataSet* data = instance.handle ? dcm_filehandle_get_metadata_subset(&error, instance.handle.get()) : nullptr;
        const char* syntax = data ? dcm_filehandle_get_transfer_syntax_uid(instance.handle.get()) : nullptr;
        if (!syntax) {
            const std::string message = error && dcm_error_get_message(error) ? dcm_error_get_message(error) : "no transfer syntax";
            dcm_error_clear(&error);
            FAIL_CHECK(entry->path().filename() << " is unreadable: " << message);
            continue;
        }
        instance.syntax = syntax;
        try {
            if (IrisSlide::source_codec(instance.syntax) == IrisSlide::SourceCodec::JPEG) continue;
        } catch (const std::runtime_error&) {
            continue;   // a syntax the encoder does not decode is not this test's input
        }
        instance.sop_uid = text_of(data, TAG_SOP_INSTANCE_UID);
        instance.shape   = {.columns = integer_of(data, TAG_COLUMNS), .rows = integer_of(data, TAG_ROWS)};
        instance.color   = IrisSlide::frame_color(IrisSlide::source_codec(instance.syntax),
                                                  text_of(data, TAG_PHOTOMETRIC_INTERPRETATION));
        // PS3.3 C.7.6.6: an instance without NumberOfFrames holds one frame.
        instance.frames  = dcm_dataset_get(nullptr, data, TAG_NUMBER_OF_FRAMES)
                         ? static_cast<std::uint32_t>(std::stoul(text_of(data, TAG_NUMBER_OF_FRAMES))) : 1;   // IS: text
        REQUIRE(dcm_filehandle_prepare_read_frame(nullptr, instance.handle.get()));
        found.push_back(std::move(instance));
    }
    return found;
}

/// Frame numbers (1-based, DICOM's) spread over an instance, the first and last included.
std::vector<std::uint32_t> sampled_frames(std::uint32_t frames)
{
    std::vector<std::uint32_t> numbers;
    for (std::uint32_t i = 0; i < SAMPLED_FRAMES && i < frames; ++i)
        numbers.push_back(1 + static_cast<std::uint32_t>(std::uint64_t{i} * (frames - 1) / (SAMPLED_FRAMES - 1)));
    return numbers;
}
} // namespace

TEST_CASE("Every JPEG 2000 and HTJ2K slide in the local folder decodes at its declared frame size",
          "[.local][Testing_01][Dicom_05]")
{
    const auto instances = jpeg2000_instances(slide_folder());
    REQUIRE_FALSE(instances.empty());
    for (const auto& instance : instances) {
        INFO(instance.path.filename() << " (" << instance.syntax << ", " << instance.frames << " frames)");
        for (const auto number : sampled_frames(instance.frames))
            CHECK_NOTHROW(instance.decode(number));
    }
}

TEST_CASE("An HTJ2K copy of a lossless JPEG 2000 slide decodes to the same pixels, frame for frame",
          "[.local][Testing_01][Dicom_05]")
{
    const auto instances = jpeg2000_instances(slide_folder());
    std::map<std::string, const Instance*> lossless_by_uid;
    for (const auto& instance : instances)
        if (instance.syntax == JPEG_2000_LOSSLESS) lossless_by_uid[instance.sop_uid] = &instance;
    std::size_t pairs = 0;
    for (const auto& copy : instances) {
        if (IrisSlide::source_codec(copy.syntax) != IrisSlide::SourceCodec::HTJ2K) continue;
        const auto original = lossless_by_uid.find(copy.sop_uid);
        if (original == lossless_by_uid.end()) continue;
        ++pairs;
        INFO(copy.path.filename() << " against " << original->second->path.filename());
        REQUIRE(copy.frames == original->second->frames);
        for (const auto number : sampled_frames(copy.frames))
            CHECK(copy.decode(number) == original->second->decode(number));
    }
    REQUIRE(pairs > 0);   // the folder holds at least one HTJ2K copy and its JPEG 2000 original
}

TEST_CASE("Each local slide's series yields its highest VOLUME instance, TILED_FULL, with its spacing",
          "[.local][Testing_01][Dicom_03]")
{
    const auto instances = jpeg2000_instances(slide_folder());
    REQUIRE_FALSE(instances.empty());
    for (const auto& local : instances) {
        INFO(local.path.filename());
        const auto series = IrisSlide::read_dicom_series(local.path);
        const auto& source = IrisSlide::highest_volume(series);
        CHECK(source.dimension_organization == "TILED_FULL");
        // TILED_FULL with one focal plane and one optical path: one frame per tile, row-major.
        const auto across = (source.total_columns + source.columns - 1) / source.columns;
        const auto down   = (source.total_rows + source.rows - 1) / source.rows;
        CHECK(std::uint64_t{source.frames} == across * down);
        REQUIRE(source.spacing);
        CHECK(source.spacing->row_mm > 0.0);
        CHECK(source.spacing->column_mm > 0.0);
        WARN(source.path.filename() << ": " << source.total_columns << " x " << source.total_rows << ", "
             << source.columns << " px frames, spacing " << source.spacing->row_mm << " x "
             << source.spacing->column_mm << " mm");
    }
}

namespace {
constexpr std::uint32_t RGBA_CHANNELS = 4;
using IrisSlide::ALPHA_OPAQUE;
constexpr double JPEG_PSNR_FLOOR_DB = 35.0;   // quality 90, 4:2:2 on stained tissue
constexpr auto ENCODER_POLL = std::chrono::milliseconds(50);

std::vector<std::uint8_t> tile_buffer()
{
    return std::vector<std::uint8_t>(std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA_CHANNELS);
}

double psnr_rgb(const std::vector<std::uint8_t>& a, const std::uint8_t* b)
{
    double squared = 0;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i % RGBA_CHANNELS == 3) continue;
        const double difference = double(a[i]) - double(b[i]);
        squared += difference * difference;
        ++samples;
    }
    const double mse = squared / double(samples);
    return mse == 0 ? INFINITY : 10.0 * std::log10(255.0 * 255.0 / mse);
}
} // namespace

TEST_CASE("An HTJ2K slide re-cut into 256-pixel tiles equals its lossless JPEG 2000 original's re-cut",
          "[.local][Testing_01][Encode_10]")
{
    const auto instances = jpeg2000_instances(slide_folder());
    std::map<std::string, const Instance*> lossless_by_uid;
    for (const auto& instance : instances)
        if (instance.syntax == JPEG_2000_LOSSLESS) lossless_by_uid[instance.sop_uid] = &instance;
    std::size_t pairs = 0;
    for (const auto& copy : instances) {
        if (IrisSlide::source_codec(copy.syntax) != IrisSlide::SourceCodec::HTJ2K) continue;
        const auto original = lossless_by_uid.find(copy.sop_uid);
        if (original == lossless_by_uid.end()) continue;
        ++pairs;
        INFO(copy.path.filename());
        IrisSlide::DicomSource htj2k(IrisSlide::read_dicom_instance(copy.path));
        IrisSlide::DicomSource j2k(IrisSlide::read_dicom_instance(original->second->path));
        const auto grid = htj2k.size();
        REQUIRE(IrisSlide::tiles_across(grid) == IrisSlide::tiles_across(j2k.size()));
        // A band of tiles in row-major order, the order the encoder reads, then the far corner.
        auto a = tile_buffer(), b = tile_buffer();
        const std::uint32_t band = IrisSlide::tiles_down(grid) / 2;
        for (std::uint32_t x = 0; x < IrisSlide::tiles_across(grid); ++x) {
            htj2k.read_tile(x, band, a);
            j2k.read_tile(x, band, b);
            CHECK(a == b);
        }
        htj2k.read_tile(IrisSlide::tiles_across(grid) - 1, IrisSlide::tiles_down(grid) - 1, a);
        j2k.read_tile(IrisSlide::tiles_across(grid) - 1, IrisSlide::tiles_down(grid) - 1, b);
        CHECK(a == b);
    }
    REQUIRE(pairs > 0);
}

TEST_CASE("A re-cut tile's pixels are the source frames' pixels at the same slide position",
          "[.local][Testing_01][Encode_10]")
{
    for (const auto& local : jpeg2000_instances(slide_folder())) {
        INFO(local.path.filename());
        IrisSlide::DicomSource source(IrisSlide::read_dicom_instance(local.path));
        const auto grid = source.size();
        std::mt19937 random(20261127);
        auto tile = tile_buffer();
        const std::uint32_t tx = IrisSlide::tiles_across(grid) / 3, ty = IrisSlide::tiles_down(grid) / 3;
        source.read_tile(tx, ty, tile);
        for (int probe = 0; probe < 32; ++probe) {
            const std::uint32_t x = random() % IrisSlide::RECUT_TILE, y = random() % IrisSlide::RECUT_TILE;
            const std::uint64_t gx = std::uint64_t{tx} * IrisSlide::RECUT_TILE + x;
            const std::uint64_t gy = std::uint64_t{ty} * IrisSlide::RECUT_TILE + y;
            const auto frame_number = static_cast<std::uint32_t>(
                (gy / local.shape.rows) * ((grid.width + local.shape.columns - 1) / local.shape.columns) +
                gx / local.shape.columns + 1);
            const auto frame = local.decode(frame_number);
            const std::size_t at = ((gy % local.shape.rows) * local.shape.columns + gx % local.shape.columns) * RGB_CHANNELS;
            const auto* px = &tile[(std::size_t{y} * IrisSlide::RECUT_TILE + x) * RGBA_CHANNELS];
            CHECK((px[0] == frame[at] && px[1] == frame[at + 1] && px[2] == frame[at + 2] && px[3] == ALPHA_OPAQUE));
        }
    }
}

TEST_CASE("open_source opens a local DICOM slide as its highest instance, with its measurements",
          "[.local][Testing_01][Encode_10]")
{
    for (const auto& local : jpeg2000_instances(slide_folder())) {
        INFO(local.path.filename());
        const auto source = IrisSlide::open_source(local.path);
        REQUIRE(source);
        CHECK(source->size().width == IrisSlide::read_dicom_instance(local.path).total_columns);
        CHECK(source->measurements().microns_per_pixel > 0.0);
        CHECK(source->measurements().magnification > 0.0);
        CHECK_FALSE(source->measurements().icc_profile.empty());
    }
}

TEST_CASE("The encoder reads an HTJ2K slide directly and writes every layer, its base close to the re-cut",
          "[.local][Testing_01][Encode_10]")
{
    std::filesystem::path htj2k;
    for (const auto& local : jpeg2000_instances(slide_folder()))
        if (htj2k.empty() && IrisSlide::source_codec(local.syntax) == IrisSlide::SourceCodec::HTJ2K) htj2k = local.path;
    REQUIRE_FALSE(htj2k.empty());
    const auto out = std::filesystem::temp_directory_path() / ("IrisSlideTest_htj2k_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(out);
    struct RemoveFolder {
        std::filesystem::path path;
        ~RemoveFolder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }   // nothing to report if absent
    } const removal {out};
    IrisSlide::Encoder encoder;
    const auto created = IrisSlide::create_encoder({.source = htj2k, .destination = out}, encoder);
    INFO(created.message);
    REQUIRE(created.succeeded());
    REQUIRE(IrisSlide::dispatch_encoder(encoder).succeeded());
    IrisSlide::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(ENCODER_POLL);
        REQUIRE(IrisSlide::encoder_progress(encoder, progress).succeeded());
    } while (progress.state == IrisSlide::EncoderState::ACTIVE);
    INFO(progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    CHECK(progress.file.filename() == htj2k.stem().string() + ".jpeg.iris");

    const auto slide = IrisSlide::open_slide({.filePath = progress.file.string()});
    REQUIRE(slide);
    IrisSlide::SlideInfo info;
    REQUIRE(IrisSlide::get_slide_info(slide, info).succeeded());
    IrisSlide::DicomSource source(IrisSlide::read_dicom_instance(htj2k));
    const auto& base = info.extent.layers.back();
    CHECK(base.xTiles == IrisSlide::tiles_across(source.size()));
    CHECK(base.yTiles == IrisSlide::tiles_down(source.size()));
    CHECK(info.extent.layers.front().xTiles == 1);
    CHECK(info.extent.layers.front().yTiles == 1);
    CHECK(info.metadata.micronsPerPixel > 0.f);
    CHECK_FALSE(info.metadata.ICC_profile.empty());
    CHECK(info.metadata.attributes.empty());   // only measurements are carried from DICOM (D2)
    auto recut = tile_buffer();
    const std::uint32_t tx = base.xTiles / 2, ty = base.yTiles / 2;
    source.read_tile(tx, ty, recut);
    const auto decoded = IrisSlide::read_slide_tile({.slide = slide,
        .layerIndex = static_cast<std::uint32_t>(info.extent.layers.size() - 1),
        .tileIndex = ty * base.xTiles + tx, .desiredFormat = Iris::FORMAT_R8G8B8A8});
    REQUIRE(decoded);
    const double psnr = psnr_rgb(recut, static_cast<const std::uint8_t*>(decoded->data()));
    INFO("PSNR " << psnr << " dB");
    CHECK(psnr >= JPEG_PSNR_FLOOR_DB);
}

TEST_CASE("Each local slide's frame index gives libdicom's exact frame bytes", "[.local][Testing_01][Dicom_06]")
{
    for (const auto& local : jpeg2000_instances(slide_folder())) {
        INFO(local.path.filename());
        const IrisSlide::DicomFrameIndex index(local.path, local.frames);
        REQUIRE(index.frames() == local.frames);
        std::vector<std::uint8_t> joined;
        for (const auto number : sampled_frames(local.frames)) {
            DcmError* error = nullptr;
            std::unique_ptr<DcmFrame, decltype(&dcm_frame_destroy)> frame(
                dcm_filehandle_read_frame(&error, local.handle.get(), number), &dcm_frame_destroy);
            REQUIRE(frame);
            const auto* bytes = static_cast<const std::uint8_t*>(static_cast<const void*>(dcm_frame_get_value(frame.get())));
            const auto indexed = index.frame(number - 1, joined);
            CHECK(std::vector<std::uint8_t>(indexed.begin(), indexed.end()) ==
                  std::vector<std::uint8_t>(bytes, bytes + dcm_frame_get_length(frame.get())));
        }
    }
}
