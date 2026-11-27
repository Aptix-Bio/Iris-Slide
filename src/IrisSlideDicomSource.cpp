//
//  IrisSlideDicomSource.cpp
//  Iris
//
//  libdicom reads each frame's bytes; decode_frame decodes them by the instance's codec; the
//  re-cut assembles 256-pixel tiles from the decoded frames (recut_tile).
//
#include "IrisSlideDicomSource.hpp"

#include <algorithm>
#include <array>
#include <thread>
#include <fstream>
#include <stdexcept>
#include <string>

extern "C" {
#include <dicom/dicom.h>
}

#include "IrisSlideFrameCodec.hpp"

namespace IrisSlide {
namespace {
constexpr std::size_t RGB = 3;
constexpr std::string_view TILED_FULL = "TILED_FULL";
constexpr std::size_t DICOM_PREAMBLE = 128;                         // PS3.10 7.1
constexpr std::array<char, 4> DICOM_PREFIX {'D', 'I', 'C', 'M'};
constexpr double MICRONS_PER_MILLIMETRE = 1000.0;

std::string take_error (DcmError*& error)
{
    const char* message = error ? dcm_error_get_message(error) : nullptr;
    std::string text = message ? message : "no detail";
    dcm_error_clear(&error);
    return text;
}
[[noreturn]] void refuse (const DicomInstance& instance, const std::string& why)
{
    throw std::runtime_error("DICOM instance " + instance.path.string() + " " + why);
}
} // namespace

// RTM Encode_10
bool is_dicom_file (const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::array<char, DICOM_PREFIX.size()> prefix {};
    file.seekg(DICOM_PREAMBLE);
    return file.read(prefix.data(), prefix.size()) && prefix == DICOM_PREFIX;
}

std::unique_ptr<Source> open_dicom_source (const std::filesystem::path& path)
{
    const auto series = read_dicom_series(path);
    return std::make_unique<DicomSource>(highest_volume(series));
}

// RTM Encode_10, Dicom_05
DicomSource::DicomSource (const DicomInstance& instance) :
_instance       (instance),
_codec          (source_codec(instance.transfer_syntax)),   // refuses a syntax outside the table
_grid           {.width = instance.total_columns, .height = instance.total_rows,
                 .frame = {.columns = instance.columns, .rows = instance.rows}},
_measurements   {.microns_per_pixel = instance.spacing.column_mm * MICRONS_PER_MILLIMETRE,
                 .magnification     = instance.objective_power,
                 .icc_profile       = instance.icc_profile}
{
    if (!decodes(_codec))
        refuse(instance, "has transfer syntax " + instance.transfer_syntax + ", whose frames this encoder does not decode");
    if (instance.dimension_organization != TILED_FULL)
        refuse(instance, "has DimensionOrganizationType \"" + instance.dimension_organization +
                         "\"; only TILED_FULL frames are in row-major order");
    if (instance.columns == 0 || instance.rows == 0)
        refuse(instance, "declares frames of " + std::to_string(instance.columns) + " x " +
                         std::to_string(instance.rows) + " pixels");
    _frames_across = static_cast<std::uint32_t>((instance.total_columns + instance.columns - 1) / instance.columns);
    _frames_down   = static_cast<std::uint32_t>((instance.total_rows + instance.rows - 1) / instance.rows);
    if (std::uint64_t{instance.frames} != std::uint64_t{_frames_across} * _frames_down)
        refuse(instance, "holds " + std::to_string(instance.frames) + " frames where one focal plane of " +
                         std::to_string(_frames_across) + " x " + std::to_string(_frames_down) + " is read");
    const std::uint32_t handles = std::max(1u, std::thread::hardware_concurrency());
    for (std::uint32_t i = 0; i < handles; ++i) _handles.push_back(open_handle());
    _claimed = std::make_unique<std::atomic<bool>[]>(handles);
}

DicomSource::~DicomSource () = default;

DicomSource::Handle DicomSource::open_handle () const
{
    DcmError* error = nullptr;
    Handle handle(dcm_filehandle_create_from_file(&error, _instance.path.string().c_str()), &dcm_filehandle_destroy);
    if (!handle) refuse(_instance, "did not open: " + take_error(error));
    if (!dcm_filehandle_get_metadata_subset(&error, handle.get()) ||
        !dcm_filehandle_prepare_read_frame(&error, handle.get()))
        refuse(_instance, "did not prepare its frames: " + take_error(error));
    return handle;
}

std::vector<std::vector<std::uint8_t>> DicomSource::read_frames (std::uint32_t first_column, std::uint32_t last_column,
                                                                std::uint32_t first_row, std::uint32_t last_row)
{
    // Claim a free handle by compare-and-swap; a reader that finds every handle claimed opens its
    // own for this read instead of waiting for one.
    std::size_t slot = 0;
    for (bool expected = false; slot < _handles.size(); ++slot, expected = false)
        if (_claimed[slot].compare_exchange_strong(expected, true)) break;
    Handle own(nullptr, &dcm_filehandle_destroy);
    if (slot == _handles.size()) own = open_handle();
    DcmFilehandle* handle = own ? own.get() : _handles[slot].get();
    std::vector<std::vector<std::uint8_t>> frames;
    try {
        for (std::uint32_t row = first_row; row <= last_row; ++row)
            for (std::uint32_t column = first_column; column <= last_column; ++column) {
                const std::uint32_t number = row * _frames_across + column + 1;   // DICOM frames count from 1
                DcmError* error = nullptr;
                std::unique_ptr<DcmFrame, decltype(&dcm_frame_destroy)> frame(
                    dcm_filehandle_read_frame(&error, handle, number), &dcm_frame_destroy);
                if (!frame) refuse(_instance, "frame " + std::to_string(number) + " did not read: " + take_error(error));
                const auto* bytes = static_cast<const std::uint8_t*>(static_cast<const void*>(dcm_frame_get_value(frame.get())));
                frames.emplace_back(bytes, bytes + dcm_frame_get_length(frame.get()));
            }
    } catch (...) {
        if (!own) _claimed[slot].store(false);
        throw;
    }
    if (!own) _claimed[slot].store(false);
    return frames;
}

std::uint64_t DicomSource::read_scratch_bytes () const noexcept
{
    // A tile crosses at most ceil(255 / frame) + 1 frames on each axis; each is held decoded (RGB8)
    // and compressed, the compressed form never larger than the decoded one.
    const std::uint64_t across = (RECUT_TILE - 1) / _grid.frame.columns + 2;
    const std::uint64_t down   = (RECUT_TILE - 1) / _grid.frame.rows + 2;
    return across * down * std::uint64_t{_grid.frame.columns} * _grid.frame.rows * RGB * 2;
}

void DicomSource::read_tile (std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba)
{
    // The frames under the tile: from its first pixel to its last within the slide.
    const std::uint64_t left = std::uint64_t{x} * RECUT_TILE, top = std::uint64_t{y} * RECUT_TILE;
    if (left >= _grid.width || top >= _grid.height)
        throw std::runtime_error("tile (" + std::to_string(x) + ", " + std::to_string(y) + ") lies beyond the slide");
    const auto first_column = static_cast<std::uint32_t>(left / _grid.frame.columns);
    const auto first_row    = static_cast<std::uint32_t>(top / _grid.frame.rows);
    const auto last_column  = static_cast<std::uint32_t>((std::min<std::uint64_t>(left + RECUT_TILE, _grid.width) - 1) / _grid.frame.columns);
    const auto last_row     = static_cast<std::uint32_t>((std::min<std::uint64_t>(top + RECUT_TILE, _grid.height) - 1) / _grid.frame.rows);
    const auto codestreams = read_frames(first_column, last_column, first_row, last_row);
    const std::uint32_t columns = last_column - first_column + 1;
    std::vector<std::vector<std::uint8_t>> frames(codestreams.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        frames[i].resize(std::size_t{_grid.frame.columns} * _grid.frame.rows * RGB);
        decode_frame(_codec, codestreams[i], _grid.frame, frames[i]);
    }
    recut_tile(_grid, x, y,
               [&](std::uint32_t column, std::uint32_t row) {
                   return std::span<const std::uint8_t>(frames[(row - first_row) * columns + (column - first_column)]);
               },
               rgba);
}
} // END IRIS SLIDE NAMESPACE
