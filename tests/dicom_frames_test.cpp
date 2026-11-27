// RTM Dicom_06
// FrameIndexMapped_22.8 (decided 2026-11-27): a DICOM instance's encapsulated frames are found once,
// by walking its elements to Pixel Data (PS3.5 7.1, 7.5) and reading the Extended Offset Table, the
// Basic Offset Table or, when both are absent, the item headers (PS3.5 A.4); the file is mapped
// read-only (Iris::Memory), so any thread reads any frame with no shared file position.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlideDicomFrames.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t UNDEFINED = 0xFFFFFFFF;

void u16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void u32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
void u64(Bytes& b, std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
void tag(Bytes& b, std::uint16_t group, std::uint16_t element) { u16(b, group); u16(b, element); }
/// An explicit-VR little-endian element with a short (16-bit) length.
void short_element(Bytes& b, std::uint16_t g, std::uint16_t e, const char* vr, const std::string& value)
{
    tag(b, g, e); b.push_back(std::uint8_t(vr[0])); b.push_back(std::uint8_t(vr[1]));
    u16(b, std::uint16_t(value.size()));
    b.insert(b.end(), value.begin(), value.end());
}
/// An element with a 32-bit length (OB, OV, SQ, …): VR, two reserved bytes, the length.
void long_header(Bytes& b, std::uint16_t g, std::uint16_t e, const char* vr, std::uint32_t length)
{
    tag(b, g, e); b.push_back(std::uint8_t(vr[0])); b.push_back(std::uint8_t(vr[1])); u16(b, 0); u32(b, length);
}
void item(Bytes& b, std::uint32_t length) { tag(b, 0xFFFE, 0xE000); u32(b, length); }

struct Built {
    Bytes file;
    std::vector<Bytes> frames;   // each frame's bytes, fragments joined
};

enum class Table { EXTENDED, BASIC, EMPTY };

/// A small instance: file meta, a nested undefined-length sequence, NumberOfFrames, and Pixel Data
/// with the chosen offset table. Frame 1 is in two fragments when the table is BASIC.
Built build(Table table, std::uint32_t frames = 3)
{
    Built built;
    Bytes& b = built.file;
    b.assign(128, 0);
    b.insert(b.end(), {'D', 'I', 'C', 'M'});
    Bytes meta;
    std::string syntax = "1.2.840.10008.1.2.4.202";
    syntax.push_back('\0');                           // UI values pad to even length with NUL
    short_element(meta, 0x0002, 0x0010, "UI", syntax);
    short_element(b, 0x0002, 0x0000, "UL", std::string("\0\0\0\0", 4));
    const std::uint32_t meta_length = std::uint32_t(meta.size());
    for (int i = 0; i < 4; ++i) b[b.size() - 4 + i] = std::uint8_t(meta_length >> (8 * i));
    b.insert(b.end(), meta.begin(), meta.end());
    // A sequence of undefined length holding an item of undefined length holding another
    // undefined-length sequence: the walk must skip all of it with its explicit stack.
    long_header(b, 0x0048, 0x0105, "SQ", UNDEFINED);
    item(b, UNDEFINED);
    short_element(b, 0x0048, 0x0112, "DS", "40");
    long_header(b, 0x0040, 0x0555, "SQ", UNDEFINED);
    item(b, 4); u32(b, 0xDEADBEEF);
    tag(b, 0xFFFE, 0xE0DD); u32(b, 0);
    tag(b, 0xFFFE, 0xE00D); u32(b, 0);
    tag(b, 0xFFFE, 0xE0DD); u32(b, 0);
    short_element(b, 0x0028, 0x0008, "IS", std::to_string(frames) + (std::to_string(frames).size() % 2 ? " " : ""));

    std::mt19937 random(7);
    for (std::uint32_t f = 0; f < frames; ++f) {
        Bytes frame(10 + 6 * f);                      // even lengths, as items require
        for (auto& v : frame) v = std::uint8_t(random());
        built.frames.push_back(frame);
    }
    // Item positions relative to the first item after the Basic Offset Table.
    std::vector<std::uint64_t> starts;
    std::uint64_t at = 0;
    for (std::uint32_t f = 0; f < frames; ++f) {
        starts.push_back(at);
        const bool split = table == Table::BASIC && f == 1;
        at += split ? 8 + 4 + 8 + (built.frames[f].size() - 4) : 8 + built.frames[f].size();
    }
    if (table == Table::EXTENDED) {
        long_header(b, 0x7FE0, 0x0001, "OV", frames * 8);
        for (const auto s : starts) u64(b, s);
        long_header(b, 0x7FE0, 0x0002, "OV", frames * 8);
        for (const auto& f : built.frames) u64(b, f.size());
    }
    long_header(b, 0x7FE0, 0x0010, "OB", UNDEFINED);
    if (table == Table::BASIC) {
        item(b, frames * 4);
        for (const auto s : starts) u32(b, std::uint32_t(s));
    } else {
        item(b, 0);
    }
    for (std::uint32_t f = 0; f < frames; ++f) {
        const Bytes& frame = built.frames[f];
        if (table == Table::BASIC && f == 1) {
            item(b, 4); b.insert(b.end(), frame.begin(), frame.begin() + 4);
            item(b, std::uint32_t(frame.size() - 4)); b.insert(b.end(), frame.begin() + 4, frame.end());
            continue;
        }
        item(b, std::uint32_t(frame.size())); b.insert(b.end(), frame.begin(), frame.end());
    }
    tag(b, 0xFFFE, 0xE0DD); u32(b, 0);
    return built;
}

std::filesystem::path write(const Bytes& bytes, const char* name)
{
    const auto path = std::filesystem::temp_directory_path() / (std::string("IrisSlideTest_") + name + ".dcm");
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

void check_frames(const Built& built, const std::filesystem::path& path)
{
    const IrisSlide::DicomFrameIndex index(path, std::uint32_t(built.frames.size()));
    REQUIRE(index.frames() == built.frames.size());
    std::vector<std::uint8_t> joined;
    for (std::uint32_t f = 0; f < built.frames.size(); ++f) {
        const auto frame = index.frame(f, joined);
        INFO("frame " << f);
        CHECK(Bytes(frame.begin(), frame.end()) == built.frames[f]);
    }
}
} // namespace

TEST_CASE("Frames are found through the Extended Offset Table", "[Dicom_06]")
{
    const auto built = build(Table::EXTENDED);
    const auto path = write(built.file, "eot");
    check_frames(built, path);
    std::filesystem::remove(path);
}

TEST_CASE("Frames are found through the Basic Offset Table, a frame in two fragments joined", "[Dicom_06]")
{
    const auto built = build(Table::BASIC);
    const auto path = write(built.file, "bot");
    check_frames(built, path);
    std::filesystem::remove(path);
}

TEST_CASE("With both offset tables empty, the item headers are scanned once", "[Dicom_06]")
{
    const auto built = build(Table::EMPTY);
    const auto path = write(built.file, "scan");
    check_frames(built, path);
    std::filesystem::remove(path);
}

TEST_CASE("A frame count that disagrees with the file, and a cut-short file, are refused by name", "[Dicom_06]")
{
    const auto built = build(Table::EMPTY);
    const auto path = write(built.file, "count");
    CHECK_THROWS_WITH(IrisSlide::DicomFrameIndex(path, 5), ContainsSubstring("5") && ContainsSubstring("3"));
    std::filesystem::remove(path);
    Bytes cut = built.file;
    cut.resize(cut.size() - 20);
    const auto short_path = write(cut, "cut");
    CHECK_THROWS_WITH(IrisSlide::DicomFrameIndex(short_path, 3), ContainsSubstring(short_path.string()));
    std::filesystem::remove(short_path);
}
