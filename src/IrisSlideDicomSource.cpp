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
#include <fstream>
#include <stdexcept>
#include <string>

#include "IrisSlideFrameCodec.hpp"

namespace IrisSlide {
namespace {
constexpr std::string_view TILED_FULL = "TILED_FULL";
constexpr std::size_t DICOM_PREAMBLE = 128;                         // PS3.10 7.1
constexpr std::array<char, 4> DICOM_PREFIX {'D', 'I', 'C', 'M'};
constexpr double MICRONS_PER_MILLIMETRE = 1000.0;

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
                 .icc_profile       = instance.icc_profile},
_index          (instance.path, instance.frames)
{
    if (!is_decodable(_codec))
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
}

std::uint64_t DicomSource::read_scratch_bytes () const noexcept
{
    // A tile crosses at most ceil(255 / frame) + 1 frames on each axis, each held decoded (RGB8),
    // plus one frame's fragments joined; a compressed frame is read in place from the map.
    const std::uint64_t across = (RECUT_TILE - 1) / _grid.frame.columns + 2;
    const std::uint64_t down   = (RECUT_TILE - 1) / _grid.frame.rows + 2;
    return (across * down + 1) * std::uint64_t{_grid.frame.columns} * _grid.frame.rows * RGB_BYTES;
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
    const std::uint32_t columns = last_column - first_column + 1;
    std::vector<std::vector<std::uint8_t>> frames(std::size_t{columns} * (last_row - first_row + 1));
    std::vector<std::uint8_t> joined;   // a frame in several fragments is joined here
    for (std::uint32_t row = first_row; row <= last_row; ++row)
        for (std::uint32_t column = first_column; column <= last_column; ++column) {
            auto& pixels = frames[(row - first_row) * columns + (column - first_column)];
            pixels.resize(std::size_t{_grid.frame.columns} * _grid.frame.rows * RGB_BYTES);
            decode_frame(_codec, _index.frame(row * _frames_across + column, joined), _grid.frame, pixels);
        }
    recut_tile(_grid, x, y,
               [&](std::uint32_t column, std::uint32_t row) {
                   return std::span<const std::uint8_t>(frames[(row - first_row) * columns + (column - first_column)]);
               },
               rgba);
}
} // END IRIS SLIDE NAMESPACE
