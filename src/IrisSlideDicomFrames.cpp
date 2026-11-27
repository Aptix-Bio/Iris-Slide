//
//  IrisSlideDicomFrames.cpp
//  Iris
//
//  PS3.10 7.1: a 128-byte preamble, "DICM", the file meta group (explicit VR little endian), the
//  data set. PS3.5 7.1.2: an explicit-VR element is its tag, two VR letters, then a 16-bit length,
//  or, for the VRs of PS3.5 Table 7.1-1, two reserved bytes and a 32-bit length. PS3.5 7.5: an
//  undefined length (FFFFFFFFH) is a sequence of items ending at (FFFE,E0DD), each item ending at
//  (FFFE,E00D) when its own length is undefined. PS3.5 A.4: encapsulated Pixel Data is a Basic
//  Offset Table item followed by fragment items; PS3.3 C.7.6.3.1.8: the Extended Offset Table
//  gives each frame's item position (from the first item after the Basic Offset Table) and length.
//
#include "IrisSlideDicomFrames.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace IrisSlide {
namespace {
constexpr std::uint32_t UNDEFINED_LENGTH = 0xFFFFFFFF;
constexpr std::uint16_t ITEM_GROUP = 0xFFFE;
constexpr std::uint16_t ITEM = 0xE000, ITEM_END = 0xE00D, SEQUENCE_END = 0xE0DD;
constexpr std::uint16_t PIXEL_GROUP = 0x7FE0;
constexpr std::uint16_t EXTENDED_OFFSETS = 0x0001, EXTENDED_LENGTHS = 0x0002, PIXEL_DATA = 0x0010;
constexpr std::uint16_t META_GROUP = 0x0002, GROUP_LENGTH = 0x0000;
constexpr std::uint64_t ITEM_HEADER = 8;
constexpr std::uint64_t ITEM_LENGTH_AT = 4;   // an item's 32-bit length follows its tag
/// PS3.5 7.1.2: tag (4) and VR (2), then a 16-bit length; or two reserved bytes and a 32-bit length.
constexpr std::uint64_t SHORT_LENGTH_AT = 6, SHORT_VALUE_AT = 8, LONG_LENGTH_AT = 8, LONG_VALUE_AT = 12;
constexpr std::uint64_t VR_AT = 4, VR_SIZE = 2, ELEMENT_HEADER = 8;
/// The width of the group-length value (UL) and of each offset-table entry: PS3.5 A.4 (32-bit
/// Basic Offset Table entries), PS3.3 C.7.6.3.1.8 (64-bit Extended Offset Table entries).
constexpr std::uint32_t UL_WIDTH = 4, BASIC_WIDTH = 4, EXTENDED_WIDTH = 8;
/// The widths of a length field: 16 bits after a short VR, 32 bits after a long VR or in an item.
constexpr std::uint32_t SHORT_LENGTH_WIDTH = 2, LONG_LENGTH_WIDTH = 4;
/// A tag is its 16-bit group, then its 16-bit element number.
constexpr std::uint32_t TAG_PART_WIDTH = 2;
/// PS3.5 Table 7.1-1: the VRs whose length is 32 bits after two reserved bytes.
constexpr std::array<std::string_view, 13> LONG_VRS {"OB", "OD", "OF", "OL", "OV", "OW", "SQ", "SV", "UC", "UN", "UR", "UT", "UV"};

/// Bounds-checked little-endian reads over the mapped file, refusing naming the path and byte.
struct Reader {
    std::span<const std::uint8_t> bytes;
    const std::filesystem::path& path;

    [[noreturn]] void refuse (std::uint64_t at, const std::string& why) const
    {
        throw std::runtime_error("DICOM file " + path.string() + " at byte " + std::to_string(at) + ": " + why);
    }
    void need (std::uint64_t at, std::uint64_t count) const
    {
        if (at > bytes.size() || count > bytes.size() - at) refuse(at, "is cut short (" + std::to_string(bytes.size()) + " bytes)");
    }
    std::uint64_t value (std::uint64_t at, std::uint32_t width) const
    {
        need(at, width);
        std::uint64_t v = 0;
        for (std::uint32_t i = 0; i < width; ++i) v |= std::uint64_t{bytes[at + i]} << (8 * i);
        return v;
    }
    std::uint16_t group (std::uint64_t at) const { return static_cast<std::uint16_t>(value(at, TAG_PART_WIDTH)); }
    std::uint16_t element (std::uint64_t at) const
    {
        return static_cast<std::uint16_t>(value(at + TAG_PART_WIDTH, TAG_PART_WIDTH));
    }
};

/// An explicit-VR element's value position and length (UNDEFINED_LENGTH when undefined).
struct Element {
    std::uint64_t value_at = 0;
    std::uint32_t length = 0;
};
Element element_at (const Reader& reader, std::uint64_t at)
{
    reader.need(at, ELEMENT_HEADER);
    const std::string_view vr(reinterpret_cast<const char*>(reader.bytes.data() + at + VR_AT), VR_SIZE);
    const bool is_long = std::find(LONG_VRS.begin(), LONG_VRS.end(), vr) != LONG_VRS.end();
    if (is_long) return {.value_at = at + LONG_VALUE_AT, .length = static_cast<std::uint32_t>(reader.value(at + LONG_LENGTH_AT, LONG_LENGTH_WIDTH))};
    return {.value_at = at + SHORT_VALUE_AT, .length = static_cast<std::uint32_t>(reader.value(at + SHORT_LENGTH_AT, SHORT_LENGTH_WIDTH))};
}

/// The position after an undefined-length sequence whose items start at @p at. Items and nested
/// undefined-length sequences are walked over an explicit stack, so no depth of nesting recurses.
std::uint64_t skip_sequence (const Reader& reader, std::uint64_t at)
{
    enum class Open : std::uint8_t { SEQUENCE, ITEM };
    std::vector<Open> open {Open::SEQUENCE};
    while (!open.empty()) {
        if (reader.group(at) != ITEM_GROUP) {
            // An element inside an item; one of undefined length opens a nested sequence.
            if (open.back() != Open::ITEM) reader.refuse(at, "an element where a sequence's item was expected");
            const Element nested = element_at(reader, at);
            at = nested.value_at;
            if (nested.length == UNDEFINED_LENGTH) open.push_back(Open::SEQUENCE);
            else at += nested.length;
            continue;
        }
        const std::uint16_t marker = reader.element(at);
        const auto length = static_cast<std::uint32_t>(reader.value(at + ITEM_LENGTH_AT, LONG_LENGTH_WIDTH));
        at += ITEM_HEADER;
        const Open expected = marker == ITEM_END ? Open::ITEM : Open::SEQUENCE;
        if (open.back() != expected || (marker != ITEM && marker != ITEM_END && marker != SEQUENCE_END))
            reader.refuse(at - ITEM_HEADER, std::format("an item marker (FFFE,{:04X}) out of place", marker));
        if (marker == ITEM_END || marker == SEQUENCE_END) open.pop_back();
        else if (length == UNDEFINED_LENGTH) open.push_back(Open::ITEM);
        else at += length;
    }
    return at;
}

/// An offset table's entries, each @p width bytes. Its length is defined, inside the file and a whole
/// number of entries, so a malformed table cannot size an allocation from bytes the file lacks.
std::vector<std::uint64_t> table_of (const Reader& reader, const Element& element, std::uint32_t width)
{
    if (element.length == UNDEFINED_LENGTH) reader.refuse(element.value_at, "has an offset table of undefined length");
    reader.need(element.value_at, element.length);
    if (element.length % width != 0)
        reader.refuse(element.value_at, "has an offset table of " + std::to_string(element.length) +
                                        " bytes, not a whole number of " + std::to_string(width) + "-byte entries");
    std::vector<std::uint64_t> entries(element.length / width);
    for (std::size_t i = 0; i < entries.size(); ++i) entries[i] = reader.value(element.value_at + i * width, width);
    return entries;
}
} // namespace

// RTM Dicom_06
DicomFrameIndex::DicomFrameIndex (const std::filesystem::path& path, std::uint32_t frames) :
_path (path)
{
    const Iris::Result mapped = Iris::create_memory({.filepath = path, .read_only = true}, _map);
    if (mapped.failed()) throw std::runtime_error("DICOM file " + path.string() + " did not map: " + mapped.message);
    const Reader reader {.bytes = std::span<const std::uint8_t>(_map.base(), _map.capacity()), .path = _path};
    reader.need(DICOM_PREAMBLE, DICOM_PREFIX.size());
    if (std::memcmp(reader.bytes.data() + DICOM_PREAMBLE, DICOM_PREFIX.data(), DICOM_PREFIX.size()) != 0)
        reader.refuse(DICOM_PREAMBLE, "has no \"DICM\" prefix");

    // The file meta group's length element (0002,0000) says where the data set begins.
    const std::uint64_t meta = DICOM_PREAMBLE + DICOM_PREFIX.size();
    if (reader.group(meta) != META_GROUP || reader.element(meta) != GROUP_LENGTH)
        reader.refuse(meta, "does not begin its file meta group with (0002,0000)");
    const Element meta_length = element_at(reader, meta);
    std::uint64_t at = meta_length.value_at + UL_WIDTH + reader.value(meta_length.value_at, UL_WIDTH);

    // The data set's top-level elements, to Pixel Data, keeping the Extended Offset Table.
    std::vector<std::uint64_t> extended_offsets, extended_lengths;
    Element pixels {};
    for (;;) {
        const Element element = element_at(reader, at);
        const std::uint16_t group = reader.group(at), number = reader.element(at);
        if (group == PIXEL_GROUP && number == PIXEL_DATA) { pixels = element; break; }
        if (group == PIXEL_GROUP && number == EXTENDED_OFFSETS) extended_offsets = table_of(reader, element, EXTENDED_WIDTH);
        if (group == PIXEL_GROUP && number == EXTENDED_LENGTHS) extended_lengths = table_of(reader, element, EXTENDED_WIDTH);
        at = element.length == UNDEFINED_LENGTH ? skip_sequence(reader, element.value_at) : element.value_at + element.length;
    }
    if (pixels.length != UNDEFINED_LENGTH) reader.refuse(pixels.value_at, "holds native Pixel Data; only encapsulated frames are indexed");

    // The Basic Offset Table item, then the fragment items from `first`.
    if (reader.group(pixels.value_at) != ITEM_GROUP || reader.element(pixels.value_at) != ITEM)
        reader.refuse(pixels.value_at, "does not begin Pixel Data with its Basic Offset Table item");
    const Element basic {.value_at = pixels.value_at + ITEM_HEADER,
                         .length = static_cast<std::uint32_t>(reader.value(pixels.value_at + ITEM_LENGTH_AT, LONG_LENGTH_WIDTH))};
    const std::vector<std::uint64_t> basic_offsets = table_of(reader, basic, BASIC_WIDTH);
    const std::uint64_t first = basic.value_at + basic.length;

    // Every fragment item, in order, until the sequence delimiter.
    std::vector<std::uint64_t> item_starts;
    for (std::uint64_t item = first; reader.element(item) != SEQUENCE_END;) {
        if (reader.group(item) != ITEM_GROUP || reader.element(item) != ITEM)
            reader.refuse(item, "holds something other than a fragment item in Pixel Data");
        const std::uint64_t length = reader.value(item + ITEM_LENGTH_AT, LONG_LENGTH_WIDTH);
        reader.need(item + ITEM_HEADER, length);
        item_starts.push_back(item - first);
        _fragments.push_back({.offset = item + ITEM_HEADER, .length = length});
        item += ITEM_HEADER + length;
    }

    // Which fragments make each frame: one each from the Extended Offset Table or when there are as
    // many fragments as frames; else the Basic Offset Table's frame starts group them.
    const std::vector<std::uint64_t>& starts = !extended_offsets.empty() ? extended_offsets : basic_offsets;
    if (!starts.empty() && starts.size() != frames)
        reader.refuse(first, "has an offset table of " + std::to_string(starts.size()) + " frames where " +
                             std::to_string(frames) + " are declared");
    if (extended_offsets.empty() && !extended_lengths.empty())
        reader.refuse(first, "has Extended Offset Table Lengths without the Extended Offset Table it measures");
    if (!extended_offsets.empty() && extended_lengths.size() != extended_offsets.size())
        reader.refuse(first, "has an Extended Offset Table of " + std::to_string(extended_lengths.size()) + " lengths for " +
                             std::to_string(extended_offsets.size()) + " frames");
    if (starts.empty() && _fragments.size() != frames)
        reader.refuse(first, "holds " + std::to_string(_fragments.size()) + " fragments and no offset table, where " +
                             std::to_string(frames) + " frames are declared");
    for (std::uint32_t f = 0; f < frames; ++f) {
        const auto position = starts.empty() ? item_starts[f] : starts[f];
        const auto found = std::lower_bound(item_starts.begin(), item_starts.end(), position);
        if (found == item_starts.end() || *found != position)
            reader.refuse(first + position, "has an offset table entry for frame " + std::to_string(f) + " that starts no item");
        const auto from = static_cast<std::uint32_t>(found - item_starts.begin());
        const std::uint64_t end = f + 1 < frames ? (starts.empty() ? item_starts[f + 1] : starts[f + 1]) : UINT64_MAX;
        std::uint32_t count = 0;
        while (from + count < item_starts.size() && item_starts[from + count] < end) ++count;
        if (!extended_lengths.empty()) _fragments[from].length = std::min(_fragments[from].length, extended_lengths[f]);
        _frames.push_back({.first = from, .count = extended_offsets.empty() ? count : 1});
    }
}

std::span<const std::uint8_t> DicomFrameIndex::frame (std::uint32_t index, std::vector<std::uint8_t>& joined) const
{
    if (index >= _frames.size())
        throw std::runtime_error("DICOM file " + _path.string() + " has no frame " + std::to_string(index) +
                                 " (it holds " + std::to_string(_frames.size()) + ")");
    const Frame& frame = _frames[index];
    const std::uint8_t* base = _map.base();
    if (frame.count == 1) return {base + _fragments[frame.first].offset, _fragments[frame.first].length};
    joined.clear();
    for (std::uint32_t i = 0; i < frame.count; ++i) {
        const Fragment& fragment = _fragments[frame.first + i];
        joined.insert(joined.end(), base + fragment.offset, base + fragment.offset + fragment.length);
    }
    return joined;
}
} // END IRIS SLIDE NAMESPACE
