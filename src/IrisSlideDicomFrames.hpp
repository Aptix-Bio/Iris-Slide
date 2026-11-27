//
//  IrisSlideDicomFrames.hpp
//  Iris
//
//  A DICOM instance's encapsulated frames, found once and read from a read-only memory map, so any
//  number of threads read any frames at once with no shared file position (decided 2026-11-27).
//  libdicom still reads the tags; it reads frames through one file position per handle.
//
#ifndef IrisSlideDicomFrames_hpp
#define IrisSlideDicomFrames_hpp
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "IrisMemory.hpp"

namespace IrisSlide {
/// PS3.10 7.1: a DICOM file opens with a 128-byte preamble and the prefix "DICM".
constexpr std::uint64_t DICOM_PREAMBLE = 128;
constexpr std::string_view DICOM_PREFIX = "DICM";

// RTM Dicom_06
/// The frames of one instance whose Pixel Data is encapsulated (PS3.5 A.4) and whose data set is
/// explicit VR little endian, as every encapsulated transfer syntax the encoder reads is.
class DicomFrameIndex {
public:
    /// Maps @p path read-only (Iris::Memory) and finds its @p frames frames: by walking its
    /// top-level elements to Pixel Data, skipping nested sequences with an explicit stack, then
    /// reading the Extended Offset Table, else the Basic Offset Table, else the item headers once.
    /// Throws std::runtime_error naming the path when the file is cut short, an element is
    /// malformed, or the frames found are not @p frames.
    DicomFrameIndex (const std::filesystem::path& path, std::uint32_t frames);

    std::uint32_t frames () const noexcept { return static_cast<std::uint32_t>(_frames.size()); }

    /// Frame @p index (counted from 0) as its compressed bytes: a span into the map when the frame
    /// is one fragment, else its fragments joined into @p joined. Safe on any thread at once.
    std::span<const std::uint8_t> frame (std::uint32_t index, std::vector<std::uint8_t>& joined) const;

private:
    struct Fragment {
        std::uint64_t offset = 0;   // the fragment's first byte, in the file
        std::uint64_t length = 0;
    };
    struct Frame {
        std::uint32_t first = 0;    // its first fragment in _fragments
        std::uint32_t count = 0;
    };

    std::filesystem::path   _path;
    Iris::Memory            _map;
    std::vector<Fragment>   _fragments;
    std::vector<Frame>      _frames;
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideDicomFrames_hpp */
