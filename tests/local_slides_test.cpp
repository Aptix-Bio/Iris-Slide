// RTM Testing_01, Dicom_05
// LocalSlideInputs_22.6 and OpenJpegFrames_22.3. Decided 2026-11-27: while the encoder is
// developed, the tests that need a real slide read the maintainer's own slides from the folder the
// CMake cache variable IRIS_SLIDE_TEST_SLIDES names; no slide file enters the repository. An
// unset variable, or a folder with no JPEG 2000 or HTJ2K instance, fails by name.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <dicom/dicom.h>
}

#include "IrisSlideOpenJpeg.hpp"
#include "IrisSlideSourceCodec.hpp"

namespace {
constexpr std::uint32_t RGB_CHANNELS = 3;
constexpr std::uint32_t SAMPLED_FRAMES = 16;   // spread over the instance, first and last included
constexpr std::uint32_t TAG_SOP_INSTANCE_UID = 0x00080018;
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
        IrisSlide::decode_jpeg2000_frame(std::span(bytes, dcm_frame_get_length(frame.get())), shape, rgb);
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
          "[Testing_01][Dicom_05]")
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
          "[Testing_01][Dicom_05]")
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
