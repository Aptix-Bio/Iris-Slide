// RTM Encode_12, Encode_13
// Av1TileEncode_27 (ARCHITECTURE D9, D22, D24): a tile's focal planes as one bare AV1 stream,
// read back by dav1d. A Z-stack is emulated in the test: an in-focus plane and blurred planes
// either side of it, in Z order (-3, -2, -1, in focus, +1, +2, +3), the order Q24 gives, as a
// multi-plane source will hand them to the encoder.
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <thread>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <dav1d/dav1d.h>

#include "IrisSlideAv1Tiles.hpp"
#include "IrisSlideEncoder.hpp"
#include "IrisSlideEncoderOptions.hpp"
#include "IrisFileExtension.hpp"
#include "encoder_test_support.hpp"

using Catch::Matchers::ContainsSubstring;
using encoder_test::ScratchFolder;
using encoder_test::wait_for;

namespace {
constexpr std::size_t SIDE = IrisSlide::RECUT_TILE;
constexpr std::size_t RGBA = IrisSlide::RGBA_BYTES;
constexpr std::uint32_t QUALITY = 90;
constexpr double LUMA_PSNR_FLOOR_DB = 35.0;     // quality 90 on a smooth pattern
constexpr std::size_t Z_PLANES_EACH_SIDE = 3;   // seven planes, as a scanner's Z-stack might hold
// AV1 OBU types (AV1 specification 6.2.2) and the OBU header's fields.
constexpr std::uint8_t OBU_SEQUENCE_HEADER = 1, OBU_TEMPORAL_DELIMITER = 2;
constexpr std::uint8_t OBU_HAS_SIZE_FIELD = 0x02, OBU_EXTENSION_FLAG = 0x04;
// A temporal delimiter OBU as the stream's first two bytes: its header with the size field, and size 0.
constexpr std::uint8_t TEMPORAL_DELIMITER_HEADER = 0x12, EMPTY_SIZE = 0x00;
// dav1d's sequence-header values for 8-bit (hbd 0) and full range (color_range 1).
constexpr int EIGHT_BIT = 0, FULL_RANGE = 1;

/// A smooth tile with fine texture, as a stained slide's in-focus plane might look.
std::vector<std::uint8_t> in_focus_tile()
{
    std::vector<std::uint8_t> rgba(SIDE * SIDE * RGBA);
    for (std::size_t y = 0; y < SIDE; ++y)
        for (std::size_t x = 0; x < SIDE; ++x) {
            auto* px = &rgba[(y * SIDE + x) * RGBA];
            px[0] = static_cast<std::uint8_t>(64 + (x / 16 + y / 32) % 128 + ((x ^ y) & 7));
            px[1] = static_cast<std::uint8_t>(32 + (y * 3 / 64) % 160);
            px[2] = static_cast<std::uint8_t>(96 + (x * 5 / 128) % 96);
            px[3] = 0xFF;
        }
    return rgba;
}
/// The k-th of 2·radius + 1 samples centred on @p i, clamped to the tile.
std::size_t window_sample(std::size_t i, std::size_t k, std::size_t radius)
{
    if (i + k < radius) return 0;
    return std::min(SIDE - 1, i + k - radius);
}
/// @p tile box-blurred over (2·radius + 1) pixels along x (an above-focus plane) or y (below),
/// edges clamped, so each plane differs from the next by a little more defocus.
std::vector<std::uint8_t> defocused(const std::vector<std::uint8_t>& tile, std::size_t radius, bool along_x)
{
    std::vector<std::uint8_t> out(tile.size());
    for (std::size_t pixel = 0; pixel < SIDE * SIDE; ++pixel) {
        const std::size_t x = pixel % SIDE, y = pixel / SIDE;
        for (std::size_t c = 0; c < RGBA; ++c) {
            std::uint32_t sum = 0;
            for (std::size_t k = 0; k <= 2 * radius; ++k) {
                const std::size_t sx = along_x ? window_sample(x, k, radius) : x;
                const std::size_t sy = along_x ? y : window_sample(y, k, radius);
                sum += tile[(sy * SIDE + sx) * RGBA + c];
            }
            out[pixel * RGBA + c] = static_cast<std::uint8_t>(sum / (2 * radius + 1));
        }
    }
    return out;
}
/// The emulated Z-stack in Z order: -3, -2, -1 (blurred along y), in focus, +1, +2, +3 (along x).
std::vector<std::vector<std::uint8_t>> z_stack()
{
    const auto in_focus = in_focus_tile();
    std::vector<std::vector<std::uint8_t>> planes;
    for (std::size_t step = Z_PLANES_EACH_SIDE; step >= 1; --step) planes.push_back(defocused(in_focus, step, false));
    planes.push_back(in_focus);
    for (std::size_t step = 1; step <= Z_PLANES_EACH_SIDE; ++step) planes.push_back(defocused(in_focus, step, true));
    return planes;
}
std::vector<std::span<const std::uint8_t>> spans_of(const std::vector<std::vector<std::uint8_t>>& planes)
{
    return {planes.begin(), planes.end()};
}

/// One OBU as the stream holds it (AV1 specification 5.3).
struct Obu {
    std::uint8_t type;
    bool         has_size_field;
};
/// The stream's OBUs in order; throws on an OBU without a size field, which this walk cannot step over.
std::vector<Obu> obus_of(std::span<const std::uint8_t> stream)
{
    std::vector<Obu> out;
    std::size_t at = 0;
    while (at < stream.size()) {
        const std::uint8_t header = stream[at++];
        const Obu obu {.type = static_cast<std::uint8_t>((header >> 3) & 0x0F), .has_size_field = (header & OBU_HAS_SIZE_FIELD) != 0};
        out.push_back(obu);
        if (header & OBU_EXTENSION_FLAG) ++at;
        if (!obu.has_size_field) throw std::runtime_error("an OBU without a size field at byte " + std::to_string(at - 1));
        std::uint64_t size = 0;
        for (int shift = 0;; shift += 7) {   // leb128 (AV1 specification 4.10.5)
            if (at >= stream.size()) throw std::runtime_error("an OBU size runs past the stream's end");
            const std::uint8_t byte = stream[at++];
            size |= std::uint64_t{byte & 0x7Fu} << shift;
            if ((byte & 0x80) == 0) break;
        }
        at += size;
    }
    return out;
}

/// What dav1d reports of one decoded plane.
struct DecodedPlane {
    Dav1dSequenceHeader  sequence {};
    Dav1dFrameType       frame_type {};
    Dav1dPixelLayout     layout {};
    int                  q_index = 0;   // the frame's base quantiser index (AV1 specification 5.9.12)
    std::vector<std::uint8_t> luma;   // SIDE × SIDE
};
/// A dav1d decoder, a data buffer and a picture, each released when its owner leaves scope, a
/// failed REQUIRE included.
struct Dav1dDecoder {
    Dav1dContext* context = nullptr;
    Dav1dDecoder() = default;
    Dav1dDecoder(const Dav1dDecoder&) = delete;
    Dav1dDecoder& operator=(const Dav1dDecoder&) = delete;
    ~Dav1dDecoder() { if (context) dav1d_close(&context); }
};
struct Dav1dBytes {
    Dav1dData data {};
    Dav1dBytes() = default;
    Dav1dBytes(const Dav1dBytes&) = delete;
    Dav1dBytes& operator=(const Dav1dBytes&) = delete;
    ~Dav1dBytes() { dav1d_data_unref(&data); }
};
struct Dav1dFrame {
    Dav1dPicture picture {};
    Dav1dFrame() = default;
    Dav1dFrame(const Dav1dFrame&) = delete;
    Dav1dFrame& operator=(const Dav1dFrame&) = delete;
    ~Dav1dFrame() { dav1d_picture_unref(&picture); }
};
/// @p picture's headers and luma.
DecodedPlane plane_of(const Dav1dPicture& picture)
{
    DecodedPlane plane {.sequence = *picture.seq_hdr, .frame_type = picture.frame_hdr->frame_type, .layout = picture.p.layout,
                        .q_index = picture.frame_hdr->quant.yac};
    REQUIRE(picture.p.w == int(SIDE));
    REQUIRE(picture.p.h == int(SIDE));
    for (std::size_t y = 0; y < SIDE; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(picture.data[0]) + std::ptrdiff_t(y) * picture.stride[0];
        plane.luma.insert(plane.luma.end(), row, row + SIDE);
    }
    return plane;
}
/// Every plane dav1d decodes from @p stream, in order; one decoder thread, so planes come out as they go in.
std::vector<DecodedPlane> decode(std::span<const std::uint8_t> stream)
{
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.n_threads = 1;
    settings.max_frame_delay = 1;
    Dav1dDecoder decoder;
    REQUIRE(dav1d_open(&decoder.context, &settings) == 0);
    Dav1dBytes bytes;
    std::uint8_t* const buffer = dav1d_data_create(&bytes.data, stream.size());
    REQUIRE(buffer != nullptr);   // dav1d_data_create returns null when it cannot allocate
    std::memcpy(buffer, stream.data(), stream.size());
    std::vector<DecodedPlane> planes;
    const auto take_pictures = [&decoder, &planes] {
        for (;;) {
            Dav1dFrame frame;
            const int got = dav1d_get_picture(decoder.context, &frame.picture);
            if (got == DAV1D_ERR(EAGAIN)) return;
            REQUIRE(got == 0);
            planes.push_back(plane_of(frame.picture));
        }
    };
    while (bytes.data.sz > 0) {
        const int sent = dav1d_send_data(decoder.context, &bytes.data);
        REQUIRE((sent == 0 || sent == DAV1D_ERR(EAGAIN)));
        take_pictures();
    }
    take_pictures();   // the frames still held
    return planes;
}

double luma_psnr(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b)
{
    double squared = 0;
    for (std::size_t i = 0; i < SIDE * SIDE; ++i) squared += (double(a[i]) - double(b[i])) * (double(a[i]) - double(b[i]));
    const double mse = squared / double(SIDE * SIDE);
    return mse == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

/// A slide whose every tile is the in-focus pattern shifted by its position, white past its
/// extent; with several planes, plane p is that tile raised by p · PLANE_LIFT, so each plane is
/// told apart.
constexpr std::uint8_t PLANE_LIFT = 12;
class StackSource final : public IrisSlide::Source {
public:
    StackSource(IrisSlide::LayerSize size, std::uint32_t planes) : _size(size), _planes(planes) {}
    IrisSlide::LayerSize size() const noexcept override { return _size; }
    const IrisSlide::SourceMeasurements& measurements() const noexcept override { return _measurements; }
    std::uint32_t planes() const noexcept override { return _planes; }
    std::uint64_t read_scratch_bytes() const noexcept override { return 0; }
    void read_tile(std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) override
    {
        const auto pattern = in_focus_tile();
        if (rgba.size() != _planes * pattern.size())
            throw std::invalid_argument("a tile of " + std::to_string(_planes) + " planes takes " +
                                        std::to_string(_planes * pattern.size()) + " bytes; " + std::to_string(rgba.size()) + " were given");
        for (std::uint32_t plane = 0; plane < _planes; ++plane)
            for (std::size_t i = 0; i < pattern.size(); ++i) {
                const std::size_t pixel = i / RGBA, gx = std::size_t{x} * SIDE + pixel % SIDE, gy = std::size_t{y} * SIDE + pixel / SIDE;
                const bool inside = gx < _size.width && gy < _size.height;
                rgba[plane * pattern.size() + i] = inside ? static_cast<std::uint8_t>(pattern[i] + x + y + plane * PLANE_LIFT) : 0xFF;
            }
    }
private:
    IrisSlide::LayerSize _size;
    std::uint32_t _planes;
    IrisSlide::SourceMeasurements _measurements {.microns_per_pixel = 0.25, .magnification = 40.0, .microns_between_planes = 1.0};
};

constexpr IrisSlide::LayerSize SLIDE {.width = 600, .height = 520};   // 3 x 3 tiles, then 2 x 2, then 1

/// Encodes a StackSource of @p planes into @p folder in @p encoding at @p effort, its subsampling
/// the encoding's default; the created encoder's Result, and its last progress when created.
Iris::Result encode_stack(const ScratchFolder& folder, std::uint32_t planes, IrisSlide::Encoding encoding,
                          IrisSlide::EncoderProgress& progress, IrisSlide::Effort effort = IrisSlide::EFFORT_DEFAULT,
                          IrisSlide::LayerSize size = SLIDE, std::uint64_t budget = IrisSlide::MEMORY_BUDGET_DEFAULT,
                          std::uint32_t concurrency = std::thread::hardware_concurrency(),
                          IrisSlide::PressureProbe probe = IrisSlide::memory_pressure)
{
    IrisSlide::EncoderDesc desc {.source = folder.path / "stack.dcm", .destination = folder.path,
                                 .memory_budget = budget, .concurrency = concurrency};
    desc.encoding = encoding;
    desc.subsampling = IrisSlide::default_subsampling(encoding);
    desc.effort = effort;
    IrisSlide::Encoder encoder;
    const auto created = IrisSlide::create_encoder(desc, std::make_unique<StackSource>(size, planes), encoder, probe);
    if (created.failed()) return created;
    REQUIRE(IrisSlide::dispatch_encoder(encoder).succeeded());
    progress = wait_for(encoder);
    return created;
}
/// A system under memory pressure the whole encode long.
IrisSlide::MemoryPressure always_critical() noexcept { return IrisSlide::MemoryPressure::CRITICAL; }

/// The smallest budget create_encoder accepts for a Z-stack of @p planes and @p size at
/// @p concurrency, read from its refusal of a 1-byte budget, so no test copies the encoder's arithmetic.
std::uint64_t smallest_budget(const ScratchFolder& folder, std::uint32_t planes, IrisSlide::LayerSize size, std::uint32_t concurrency)
{
    IrisSlide::EncoderProgress progress;
    const auto refused = encode_stack(folder, planes, IrisSlide::TILE_ENCODING_AV1, progress, IrisSlide::EFFORT_DEFAULT, size, 1, concurrency);
    constexpr std::string_view BELOW = "below the ";
    const auto from = refused.message.find(BELOW);
    REQUIRE(from != std::string::npos);
    return std::stoull(refused.message.substr(from + BELOW.size()));
}
} // namespace

TEST_CASE("Every tile of an AV1 slide the encoder writes is one plane dav1d decodes, BT.601 full range", "[Encode_13]")
{
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const auto created = encode_stack(folder, 1, IrisSlide::TILE_ENCODING_AV1, progress);
    INFO(created.message << progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    Iris::File::Parser parser;
    REQUIRE(Iris::File::create_parser({.filepath = progress.file}, parser).succeeded());
    const Iris::File::Abstraction::File* file = nullptr;
    REQUIRE(parser.abstraction(file).succeeded());
    CHECK(file->tileTable.encoding == IrisSlide::TILE_ENCODING_AV1);
    std::size_t tiles = 0;
    for (std::uint32_t layer = 0; layer < file->tileTable.layers.size(); ++layer)
        for (std::uint32_t tile = 0; tile < file->tileTable.layers[layer].size(); ++tile) {
            INFO("layer " << layer << ", tile " << tile);
            std::span<const std::uint8_t> stream;
            REQUIRE(parser.tile(layer, tile, stream).succeeded());
            const auto decoded = decode(stream);
            REQUIRE(decoded.size() == 1);
            CHECK(decoded.front().frame_type == DAV1D_FRAME_TYPE_KEY);
            CHECK(decoded.front().sequence.mtrx == DAV1D_MC_BT601);
            CHECK(decoded.front().sequence.color_range == FULL_RANGE);
            ++tiles;
        }
    CHECK(tiles == 3 * 3 + 2 * 2 + 1);
}

// RTM Encode_13
TEST_CASE("A Z-stacked slide is written with every focal plane of every tile, in Z order from the first", "[Encode_13]")
{
    // ZStackSource_27.9, Q24 (resolved 2026-11-27): five planes, written in Z order, the first plane
    // the key frame and each later plane an inter frame.
    constexpr std::uint32_t STACK_PLANES = 5;
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const auto created = encode_stack(folder, STACK_PLANES, IrisSlide::TILE_ENCODING_AV1, progress);
    INFO(created.message << progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    Iris::File::Parser parser;
    REQUIRE(Iris::File::create_parser({.filepath = progress.file}, parser).succeeded());
    const Iris::File::Abstraction::File* file = nullptr;
    REQUIRE(parser.abstraction(file).succeeded());
    CHECK(std::abs(file->micronsPerPlane - 1.f) < 1e-6f);
    REQUIRE(file->tileTable.planes.size() == file->tileTable.layers.size());
    for (const auto planes : file->tileTable.planes) CHECK(planes == STACK_PLANES);
    for (std::uint32_t layer = 0; layer < file->tileTable.layers.size(); ++layer)
        for (std::uint32_t tile = 0; tile < file->tileTable.layers[layer].size(); ++tile) {
            INFO("layer " << layer << ", tile " << tile);
            std::uint16_t planes = 0;
            REQUIRE(parser.tile_planes(layer, tile, planes).succeeded());
            CHECK(planes == STACK_PLANES);
            std::span<const std::uint8_t> stream;
            REQUIRE(parser.tile(layer, tile, stream).succeeded());
            const auto decoded = decode(stream);
            REQUIRE(decoded.size() == STACK_PLANES);
            CHECK(decoded.front().frame_type == DAV1D_FRAME_TYPE_KEY);
            for (std::size_t p = 1; p < STACK_PLANES; ++p) CHECK(decoded[p].frame_type == DAV1D_FRAME_TYPE_INTER);
        }
    // At full resolution, tile (0, 0) is the source's own tile: each decoded plane matches the
    // plane of its position: frame k is plane k.
    StackSource source(SLIDE, STACK_PLANES);
    std::vector<std::uint8_t> planes(STACK_PLANES * SIDE * SIDE * RGBA);
    source.read_tile(0, 0, planes);
    const auto full = static_cast<std::uint32_t>(file->tileTable.layers.size() - 1);
    std::span<const std::uint8_t> stream;
    REQUIRE(parser.tile(full, 0, stream).succeeded());
    const auto decoded = decode(stream);
    REQUIRE(decoded.size() == STACK_PLANES);
    for (std::size_t k = 0; k < STACK_PLANES; ++k) {
        INFO("frame and plane " << k);
        const auto plane = std::span<const std::uint8_t>(planes).subspan(k * SIDE * SIDE * RGBA, SIDE * SIDE * RGBA);
        const auto source_luma = IrisSlide::tile_to_i420(plane);
        CHECK(luma_psnr(decoded[k].luma, std::span(source_luma).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
    }
}

TEST_CASE("A Z-stacked encode at the smallest budget it accepts stays within it", "[Encode_11][Encode_13]")
{
    // ZStackBudget_27.10: a five-plane encode of the maintainer's emulated Z-stack peaked at 6289 MiB
    // of a 6.0 GiB budget.
    constexpr std::uint32_t STACK_PLANES = 5, WORKERS = 4;
    constexpr IrisSlide::LayerSize WIDE {.width = 2000, .height = 2000};   // 8 x 8 tiles
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const std::uint64_t smallest = smallest_budget(folder, STACK_PLANES, WIDE, WORKERS);
    const auto created = encode_stack(folder, STACK_PLANES, IrisSlide::TILE_ENCODING_AV1, progress, IrisSlide::EFFORT_DEFAULT, WIDE, smallest, WORKERS);
    INFO(created.message << progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    INFO("peak " << progress.peak_resident_bytes << " of " << smallest);
    CHECK(progress.peak_resident_bytes <= smallest);
}

TEST_CASE("Under critical memory pressure an encode keeps to the least the slide needs, whatever its budget", "[Encode_11]")
{
    // RTM Encode_11 (D26): the budget is the ceiling, and under pressure new rows are admitted
    // against the slide's smallest budget.
    constexpr std::uint32_t STACK_PLANES = 5, WORKERS = 4;
    constexpr IrisSlide::LayerSize WIDE {.width = 2000, .height = 2000};   // 8 x 8 tiles
    constexpr std::uint64_t ROOMY = 1ull << 30;
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const std::uint64_t smallest = smallest_budget(folder, STACK_PLANES, WIDE, WORKERS);
    REQUIRE(smallest < ROOMY);
    const auto created = encode_stack(folder, STACK_PLANES, IrisSlide::TILE_ENCODING_AV1, progress, IrisSlide::EFFORT_DEFAULT, WIDE,
                                      ROOMY, WORKERS, always_critical);
    INFO(created.message << progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    INFO("peak " << progress.peak_resident_bytes << " of the smallest budget " << smallest);
    CHECK(progress.peak_resident_bytes <= smallest);
}

TEST_CASE("A JPEG encode of a Z-stacked source is refused naming its planes", "[Encode_09][Encode_13]")
{
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const auto created = encode_stack(folder, 5, IrisSlide::TILE_ENCODING_JPEG, progress);
    CHECK_THAT(created.message, ContainsSubstring("5 focal planes") && ContainsSubstring("JPEG"));
}

TEST_CASE("One focal plane is a bare AV1 stream dav1d decodes: BT.601 full range, 8-bit 4:2:0, the full still-picture header", "[Encode_13]")
{
    const auto tile = in_focus_tile();
    const std::vector<std::span<const std::uint8_t>> planes {tile};
    const auto stream = IrisSlide::encode_av1_tile(planes, QUALITY);
    REQUIRE(stream.size() > 2);
    CHECK(stream[0] == TEMPORAL_DELIMITER_HEADER);
    CHECK(stream[1] == EMPTY_SIZE);
    const auto obus = obus_of(stream);
    CHECK(obus.front().type == OBU_TEMPORAL_DELIMITER);
    const auto decoded = decode(stream);
    REQUIRE(decoded.size() == 1);
    const auto& sequence = decoded.front().sequence;
    CHECK(sequence.profile == int(IrisSlide::AV1_PROFILE_MAIN));
    CHECK(sequence.hbd == EIGHT_BIT);
    CHECK(decoded.front().layout == DAV1D_PIXEL_LAYOUT_I420);
    CHECK(sequence.mtrx == DAV1D_MC_BT601);
    CHECK(sequence.color_range == FULL_RANGE);
    CHECK(sequence.still_picture == 1);
    CHECK(sequence.reduced_still_picture_header == 0);
    CHECK(decoded.front().frame_type == DAV1D_FRAME_TYPE_KEY);
    const auto source = IrisSlide::tile_to_i420(tile);
    CHECK(luma_psnr(decoded.front().luma, std::span(source).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
}

TEST_CASE("An emulated Z-stack is one AV1 stream: the first plane the key frame, then an inter frame per plane", "[Encode_13]")
{
    const auto planes = z_stack();
    const auto stream = IrisSlide::encode_av1_tile(spans_of(planes), QUALITY);
    const auto obus = obus_of(stream);
    std::size_t delimiters = 0, sequence_headers = 0;
    for (const auto& obu : obus) {
        delimiters += obu.type == OBU_TEMPORAL_DELIMITER;
        sequence_headers += obu.type == OBU_SEQUENCE_HEADER;
    }
    CHECK(delimiters == planes.size());   // one temporal unit per plane
    CHECK(sequence_headers == 1);         // the sequence header in the first unit alone
    CHECK(obus[0].type == OBU_TEMPORAL_DELIMITER);
    CHECK(obus[1].type == OBU_SEQUENCE_HEADER);
    const auto decoded = decode(stream);
    REQUIRE(decoded.size() == planes.size());
    CHECK(decoded.front().sequence.still_picture == 0);
    CHECK(decoded.front().frame_type == DAV1D_FRAME_TYPE_KEY);
    for (std::size_t plane = 0; plane < planes.size(); ++plane) {
        INFO("plane " << plane);
        if (plane > 0) CHECK(decoded[plane].frame_type == DAV1D_FRAME_TYPE_INTER);
        CHECK(decoded[plane].sequence.mtrx == DAV1D_MC_BT601);
        CHECK(decoded[plane].sequence.color_range == FULL_RANGE);
        const auto source = IrisSlide::tile_to_i420(planes[plane]);
        CHECK(luma_psnr(decoded[plane].luma, std::span(source).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
    }
}

TEST_CASE("Every plane of a stack is coded at the quantiser its quality names, the key frame included", "[Encode_13]")
{
    // A key frame that later planes reference is given a finer quantiser by libaom's rate control
    // unless the range is pinned (found 2026-11-27: a five-plane tissue tile at quality 90 had its key
    // frame at q-index 2 and the rest at 24, 2.63 times the in-focus plane alone; on this test's
    // synthetic stack the key frame was at q-index 1).
    const auto planes = z_stack();
    const std::vector<std::span<const std::uint8_t>> one {planes.front()};
    const auto alone = decode(IrisSlide::encode_av1_tile(one, QUALITY));
    const auto stack = decode(IrisSlide::encode_av1_tile(spans_of(planes), QUALITY));
    REQUIRE(alone.size() == 1);
    REQUIRE(stack.size() == planes.size());
    for (std::size_t plane = 0; plane < stack.size(); ++plane) {
        INFO("stream position " << plane);
        CHECK(stack[plane].q_index == alone.front().q_index);
    }
}

TEST_CASE("Every effort encodes a plane at the quantiser its quality names, and an effort above EFFORT_MAX is refused naming it", "[Encode_16]")
{
    // RTM Encode_16: effort maps to libaom's speed, 6 - effort (D25); one plane keeps the slowest
    // speed's cost small.
    const auto tile = in_focus_tile();
    const std::vector<std::span<const std::uint8_t>> one {tile};
    const auto fastest = decode(IrisSlide::encode_av1_tile(one, QUALITY, IrisSlide::EFFORT_MIN));
    const auto slowest = decode(IrisSlide::encode_av1_tile(one, QUALITY, IrisSlide::EFFORT_MAX));
    REQUIRE(fastest.size() == 1);
    REQUIRE(slowest.size() == 1);
    CHECK(slowest.front().q_index == fastest.front().q_index);
    const auto source = IrisSlide::tile_to_i420(tile);
    CHECK(luma_psnr(slowest.front().luma, std::span(source).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
    CHECK_THROWS_WITH(IrisSlide::encode_av1_tile(one, QUALITY, static_cast<IrisSlide::Effort>(IrisSlide::EFFORT_MAX + 1)),
                      ContainsSubstring("effort 7"));
}

TEST_CASE("An AV1 slide encodes at an effort above the default, every tile decoding", "[Encode_16]")
{
    // RTM Encode_16: the caller's effort reaches each tile's encode through the encoder (D25).
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    const auto created = encode_stack(folder, 1, IrisSlide::TILE_ENCODING_AV1, progress, static_cast<IrisSlide::Effort>(IrisSlide::EFFORT_MIN + 1));
    INFO(created.message << progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    Iris::File::Parser parser;
    REQUIRE(Iris::File::create_parser({.filepath = progress.file}, parser).succeeded());
    const Iris::File::Abstraction::File* file = nullptr;
    REQUIRE(parser.abstraction(file).succeeded());
    for (std::uint32_t layer = 0; layer < file->tileTable.layers.size(); ++layer)
        for (std::uint32_t tile = 0; tile < file->tileTable.layers[layer].size(); ++tile) {
            std::span<const std::uint8_t> stream;
            REQUIRE(parser.tile(layer, tile, stream).succeeded());
            CHECK(decode(stream).size() == 1);
        }
}

TEST_CASE("AV1 takes 4:2:0 when no subsampling is named, and a source of 0 or over 65535 planes is refused naming it", "[Encode_09][Encode_13]")
{
    CHECK(IrisSlide::default_subsampling(IrisSlide::TILE_ENCODING_AV1) == IrisSlide::SUBSAMPLE_420);
    CHECK(IrisSlide::default_subsampling(IrisSlide::TILE_ENCODING_JPEG) == IrisSlide::SUBSAMPLE_DEFAULT);
    const ScratchFolder folder;
    IrisSlide::EncoderProgress progress;
    CHECK_THAT(encode_stack(folder, 0, IrisSlide::TILE_ENCODING_AV1, progress).message, ContainsSubstring("0 focal planes"));
    CHECK_THAT(encode_stack(folder, 65536, IrisSlide::TILE_ENCODING_AV1, progress).message, ContainsSubstring("65536 focal planes"));
}

TEST_CASE("The same planes and quality give the same AV1 bytes", "[Encode_12][Encode_13]")
{
    const auto planes = z_stack();
    CHECK(IrisSlide::encode_av1_tile(spans_of(planes), QUALITY) == IrisSlide::encode_av1_tile(spans_of(planes), QUALITY));
}

TEST_CASE("An AV1 tile with no planes, a wrong-sized plane or a quality outside 1-100 is refused naming it", "[Encode_13]")
{
    const auto tile = in_focus_tile();
    const std::vector<std::span<const std::uint8_t>> one {tile};
    CHECK_THROWS_WITH(IrisSlide::encode_av1_tile({}, QUALITY), ContainsSubstring("at least one focal plane"));
    const std::vector<std::uint8_t> short_tile(100);
    const std::vector<std::span<const std::uint8_t>> wrong {short_tile};
    CHECK_THROWS_WITH(IrisSlide::encode_av1_tile(wrong, QUALITY), ContainsSubstring("this one has 100"));
    CHECK_THROWS_WITH(IrisSlide::encode_av1_tile(one, 0), ContainsSubstring("quality 0"));
    CHECK_THROWS_WITH(IrisSlide::encode_av1_tile(one, 101), ContainsSubstring("quality 101"));
}
