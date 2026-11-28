// Synthetic DICOM files for tests, built in memory (PS3.10 7.1, PS3.5 7.1.2, 7.5, A.4), so the
// DICOM source is tested in CI with no slide hosted anywhere. A whole-slide instance is the
// smallest the behaviour needs: a few frames of a few pixels' pattern.
#ifndef IrisSlideTestDicomBuilder_hpp
#define IrisSlideTestDicomBuilder_hpp

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace dicom_builder {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t UNDEFINED = 0xFFFFFFFF;
constexpr std::size_t PREAMBLE = 128;

inline void u16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
inline void u32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void u64(Bytes& b, std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void tag(Bytes& b, std::uint16_t group, std::uint16_t element) { u16(b, group); u16(b, element); }
/// An explicit-VR little-endian element with a short (16-bit) length.
inline void short_element(Bytes& b, std::uint16_t g, std::uint16_t e, const char* vr, const std::string& value)
{
    tag(b, g, e); b.push_back(std::uint8_t(vr[0])); b.push_back(std::uint8_t(vr[1]));
    u16(b, std::uint16_t(value.size()));
    b.insert(b.end(), value.begin(), value.end());
}
/// An element with a 32-bit length (OB, OV, SQ, …): VR, two reserved bytes, the length.
inline void long_header(Bytes& b, std::uint16_t g, std::uint16_t e, const char* vr, std::uint32_t length)
{
    tag(b, g, e); b.push_back(std::uint8_t(vr[0])); b.push_back(std::uint8_t(vr[1])); u16(b, 0); u32(b, length);
}
inline void item(Bytes& b, std::uint32_t length) { tag(b, 0xFFFE, 0xE000); u32(b, length); }
inline void item_end(Bytes& b) { tag(b, 0xFFFE, 0xE00D); u32(b, 0); }
inline void sequence_end(Bytes& b) { tag(b, 0xFFFE, 0xE0DD); u32(b, 0); }

/// Text padded to even length as PS3.5 6.2 requires: UI with NUL, every other string VR with a space.
inline std::string padded(std::string text, char pad = ' ')
{
    if (text.size() % 2) text.push_back(pad);
    return text;
}
inline void text(Bytes& b, std::uint16_t g, std::uint16_t e, const char* vr, const std::string& value)
{
    short_element(b, g, e, vr, padded(value, std::string(vr) == "UI" ? '\0' : ' '));
}
inline void us(Bytes& b, std::uint16_t g, std::uint16_t e, std::uint16_t value)
{
    tag(b, g, e); b.push_back('U'); b.push_back('S'); u16(b, 2); u16(b, value);
}
inline void ul(Bytes& b, std::uint16_t g, std::uint16_t e, std::uint32_t value)
{
    tag(b, g, e); b.push_back('U'); b.push_back('L'); u16(b, 4); u32(b, value);
}
inline void fl(Bytes& b, std::uint16_t g, std::uint16_t e, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    tag(b, g, e); b.push_back('F'); b.push_back('L'); u16(b, 4); u32(b, bits);
}

/// The 128-byte preamble, "DICM" and the file meta group for @p transfer_syntax.
inline void file_meta(Bytes& b, const std::string& transfer_syntax, const std::string& sop_class, const std::string& sop)
{
    b.assign(PREAMBLE, 0);
    b.insert(b.end(), {'D', 'I', 'C', 'M'});
    Bytes meta;
    long_header(meta, 0x0002, 0x0001, "OB", 2); meta.push_back(0); meta.push_back(1);
    text(meta, 0x0002, 0x0002, "UI", sop_class);
    text(meta, 0x0002, 0x0003, "UI", sop);
    text(meta, 0x0002, 0x0010, "UI", transfer_syntax);
    text(meta, 0x0002, 0x0012, "UI", "1.2.826.0.1.3680043.10.1");
    ul(b, 0x0002, 0x0000, std::uint32_t(meta.size()));
    b.insert(b.end(), meta.begin(), meta.end());
}

/// A VL Whole Slide Microscopy instance, TILED_FULL, one focal plane, with @p frames encapsulated
/// one fragment each in raster order.
struct WholeSlide {
    std::uint32_t      width = 0, height = 0, frame_columns = 0, frame_rows = 0;
    std::string        transfer_syntax = "1.2.840.10008.1.2.4.50";
    std::string        photometric = "YBR_FULL_422";
    std::string        image_flavor = "VOLUME";                 ///< ImageType value 3
    std::string        pixel_spacing = "0.00025\\0.00025";       ///< empty: no SharedFunctionalGroupsSequence
    std::string        series_uid = "1.2.826.0.1.3680043.10.2";
    std::string        sop_uid = "1.2.826.0.1.3680043.10.3";
    std::uint32_t      focal_planes = 1;       ///< more than 1: TotalPixelMatrixFocalPlanes is written
    float              plane_spacing_mm = 0;   ///< DistanceBetweenFocalPlanes, written with the planes
    std::vector<Bytes> frames;                 ///< every position of plane 0, then of plane 1, ...
};
inline Bytes whole_slide(const WholeSlide& slide)
{
    constexpr const char* WSI_CLASS = "1.2.840.10008.5.1.4.1.1.77.1.6";
    Bytes b;
    file_meta(b, slide.transfer_syntax, WSI_CLASS, slide.sop_uid);
    text(b, 0x0008, 0x0008, "CS", "DERIVED\\PRIMARY\\" + slide.image_flavor + "\\NONE");
    text(b, 0x0008, 0x0016, "UI", WSI_CLASS);
    text(b, 0x0008, 0x0018, "UI", slide.sop_uid);
    text(b, 0x0020, 0x000D, "UI", "1.2.826.0.1.3680043.10.4");
    text(b, 0x0020, 0x000E, "UI", slide.series_uid);
    text(b, 0x0020, 0x9311, "CS", "TILED_FULL");
    us(b, 0x0028, 0x0002, 3);                                          // SamplesPerPixel
    text(b, 0x0028, 0x0004, "CS", slide.photometric);
    us(b, 0x0028, 0x0006, 0);                                          // PlanarConfiguration
    text(b, 0x0028, 0x0008, "IS", std::to_string(slide.frames.size()));
    us(b, 0x0028, 0x0010, std::uint16_t(slide.frame_rows));
    us(b, 0x0028, 0x0011, std::uint16_t(slide.frame_columns));
    us(b, 0x0028, 0x0100, 8); us(b, 0x0028, 0x0101, 8); us(b, 0x0028, 0x0102, 7); us(b, 0x0028, 0x0103, 0);
    ul(b, 0x0048, 0x0006, slide.width);
    ul(b, 0x0048, 0x0007, slide.height);
    if (slide.focal_planes > 1) {
        fl(b, 0x0048, 0x0014, slide.plane_spacing_mm);   // DistanceBetweenFocalPlanes
        ul(b, 0x0048, 0x0303, slide.focal_planes);       // TotalPixelMatrixFocalPlanes
    }
    // SharedFunctionalGroupsSequence › PixelMeasuresSequence › PixelSpacing, undefined lengths.
    if (!slide.pixel_spacing.empty()) {
        long_header(b, 0x5200, 0x9229, "SQ", UNDEFINED);
        item(b, UNDEFINED);
        long_header(b, 0x0028, 0x9110, "SQ", UNDEFINED);
        item(b, UNDEFINED);
        text(b, 0x0028, 0x0030, "DS", slide.pixel_spacing);
        item_end(b); sequence_end(b);
        item_end(b); sequence_end(b);
    }
    long_header(b, 0x7FE0, 0x0010, "OB", UNDEFINED);
    item(b, 0);                                                        // an empty Basic Offset Table
    for (Bytes frame : slide.frames) {
        if (frame.size() % 2) frame.push_back(0);                      // items are even (PS3.5 A.4)
        item(b, std::uint32_t(frame.size()));
        b.insert(b.end(), frame.begin(), frame.end());
    }
    sequence_end(b);
    return b;
}

/// @p bytes written as @p name in its own folder under the temporary directory, so a series read
/// finds this instance alone.
inline std::filesystem::path write_instance(const Bytes& bytes, const std::string& name)
{
    const auto folder = std::filesystem::temp_directory_path() / ("IrisSlideTest_" + name);
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto path = folder / "instance.dcm";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}
/// @p instances written as instance_0.dcm, instance_1.dcm, … in one new folder, a series; returns
/// their paths in order.
inline std::vector<std::filesystem::path> write_series(const std::vector<Bytes>& instances, const std::string& name)
{
    const auto folder = std::filesystem::temp_directory_path() / ("IrisSlideTest_" + name);
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::vector<std::filesystem::path> paths;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        paths.push_back(folder / ("instance_" + std::to_string(i) + ".dcm"));
        std::ofstream(paths.back(), std::ios::binary)
            .write(reinterpret_cast<const char*>(instances[i].data()), std::streamsize(instances[i].size()));
    }
    return paths;
}
} // namespace dicom_builder
#endif /* IrisSlideTestDicomBuilder_hpp */
