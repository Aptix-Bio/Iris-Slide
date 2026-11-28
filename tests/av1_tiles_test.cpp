// RTM Encode_12, Encode_13
// Av1TileEncode_27 (ARCHITECTURE D9, D22, D24): a tile's focal planes as one bare AV1 stream,
// read back by dav1d. A Z-stack is emulated in the test: an in-focus plane and blurred planes
// either side of it, in the stream order D9 gives (in focus, +1, -1, +2, -2, +3, -3), as a
// multi-plane source will hand them to the encoder.
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
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <dav1d/dav1d.h>

#include "IrisSlideAv1Tiles.hpp"
#include "IrisSlideEncoder.hpp"
#include "IrisFileExtension.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::size_t SIDE = IrisSlide::RECUT_TILE;
constexpr std::size_t RGBA = IrisSlide::RGBA_BYTES;
constexpr std::uint32_t QUALITY = 90;
constexpr double LUMA_PSNR_FLOOR_DB = 35.0;     // quality 90 on a smooth pattern
constexpr std::size_t Z_PLANES_EACH_SIDE = 3;   // seven planes, as a scanner's Z-stack might hold
// AV1 OBU types (AV1 specification 6.2.2) and the OBU header's fields.
constexpr std::uint8_t OBU_SEQUENCE_HEADER = 1, OBU_TEMPORAL_DELIMITER = 2;
constexpr std::uint8_t OBU_HAS_SIZE_FIELD = 0x02, OBU_EXTENSION_FLAG = 0x04;

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
/// @p tile box-blurred over (2·radius + 1) pixels along x (an above-focus plane) or y (below),
/// edges clamped, so each plane differs from the next by a little more defocus.
std::vector<std::uint8_t> defocused(const std::vector<std::uint8_t>& tile, std::size_t radius, bool along_x)
{
    std::vector<std::uint8_t> out(tile.size());
    for (std::size_t y = 0; y < SIDE; ++y)
        for (std::size_t x = 0; x < SIDE; ++x)
            for (std::size_t c = 0; c < RGBA; ++c) {
                std::uint32_t sum = 0;
                for (std::size_t k = 0; k <= 2 * radius; ++k) {
                    const std::size_t at = std::min(SIDE - 1, (along_x ? x : y) + k >= radius ? (along_x ? x : y) + k - radius : 0);
                    sum += tile[((along_x ? y : at) * SIDE + (along_x ? at : x)) * RGBA + c];
                }
                out[(y * SIDE + x) * RGBA + c] = static_cast<std::uint8_t>(sum / (2 * radius + 1));
            }
    return out;
}
/// The emulated Z-stack in stream order: in focus, then +1, -1, +2, -2, +3, -3.
std::vector<std::vector<std::uint8_t>> z_stack()
{
    std::vector<std::vector<std::uint8_t>> planes {in_focus_tile()};
    for (std::size_t step = 1; step <= Z_PLANES_EACH_SIDE; ++step) {
        planes.push_back(defocused(planes.front(), step, true));
        planes.push_back(defocused(planes.front(), step, false));
    }
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
    std::vector<std::uint8_t> luma;   // SIDE × SIDE
};
/// @p picture's headers and luma, the picture released.
DecodedPlane plane_of(Dav1dPicture& picture)
{
    DecodedPlane plane {.sequence = *picture.seq_hdr, .frame_type = picture.frame_hdr->frame_type, .layout = picture.p.layout};
    REQUIRE(picture.p.w == int(SIDE));
    REQUIRE(picture.p.h == int(SIDE));
    for (std::size_t y = 0; y < SIDE; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(picture.data[0]) + std::ptrdiff_t(y) * picture.stride[0];
        plane.luma.insert(plane.luma.end(), row, row + SIDE);
    }
    dav1d_picture_unref(&picture);
    return plane;
}
/// Every plane dav1d decodes from @p stream, in order; one decoder thread, so planes come out as they go in.
std::vector<DecodedPlane> decode(std::span<const std::uint8_t> stream)
{
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.n_threads = 1;
    settings.max_frame_delay = 1;
    Dav1dContext* context = nullptr;
    REQUIRE(dav1d_open(&context, &settings) == 0);
    Dav1dData data {};
    std::memcpy(dav1d_data_create(&data, stream.size()), stream.data(), stream.size());
    std::vector<DecodedPlane> planes;
    Dav1dPicture picture {};
    while (data.sz > 0) {
        const int sent = dav1d_send_data(context, &data);
        REQUIRE((sent == 0 || sent == DAV1D_ERR(EAGAIN)));
        for (int got = dav1d_get_picture(context, &picture); got != DAV1D_ERR(EAGAIN); got = dav1d_get_picture(context, &picture)) {
            REQUIRE(got == 0);
            planes.push_back(plane_of(picture));
        }
    }
    while (dav1d_get_picture(context, &picture) == 0) planes.push_back(plane_of(picture));   // the frames still held
    dav1d_data_unref(&data);
    dav1d_close(&context);
    return planes;
}

double luma_psnr(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b)
{
    double squared = 0;
    for (std::size_t i = 0; i < SIDE * SIDE; ++i) squared += (double(a[i]) - double(b[i])) * (double(a[i]) - double(b[i]));
    const double mse = squared / double(SIDE * SIDE);
    return mse == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

/// A slide whose every tile is the in-focus pattern shifted by its position, white past its extent.
class StackSource final : public IrisSlide::Source {
public:
    explicit StackSource(IrisSlide::LayerSize size) : _size(size) {}
    IrisSlide::LayerSize size() const noexcept override { return _size; }
    const IrisSlide::SourceMeasurements& measurements() const noexcept override { return _measurements; }
    std::uint64_t read_scratch_bytes() const noexcept override { return 0; }
    void read_tile(std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) override
    {
        const auto pattern = in_focus_tile();
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const std::size_t pixel = i / RGBA, gx = std::size_t{x} * SIDE + pixel % SIDE, gy = std::size_t{y} * SIDE + pixel / SIDE;
            const bool inside = gx < _size.width && gy < _size.height;
            rgba[i] = inside ? static_cast<std::uint8_t>(pattern[i] + x + y) : 0xFF;
        }
    }
private:
    IrisSlide::LayerSize _size;
    IrisSlide::SourceMeasurements _measurements {.microns_per_pixel = 0.25, .magnification = 40.0};
};
} // namespace

TEST_CASE("Every tile of an AV1 slide the encoder writes is one plane dav1d decodes, BT.601 full range", "[Encode_13]")
{
    const auto folder = std::filesystem::temp_directory_path() / ("IrisSlideTest_av1_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(folder);
    IrisSlide::EncoderDesc desc {.source = folder / "stack.dcm", .destination = folder};
    desc.encoding = IrisSlide::TILE_ENCODING_AV1;
    desc.subsampling = IrisSlide::SUBSAMPLE_420;
    IrisSlide::Encoder encoder;
    const auto created = IrisSlide::create_encoder(desc, std::make_unique<StackSource>(IrisSlide::LayerSize{.width = 600, .height = 520}), encoder);
    INFO(created.message);
    REQUIRE(created.succeeded());
    REQUIRE(IrisSlide::dispatch_encoder(encoder).succeeded());
    IrisSlide::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        REQUIRE(IrisSlide::encoder_progress(encoder, progress).succeeded());
    } while (progress.state == IrisSlide::EncoderState::ACTIVE);
    INFO(progress.error);
    REQUIRE(progress.state == IrisSlide::EncoderState::DONE);
    {
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
                CHECK(decoded.front().sequence.color_range == 1);
                ++tiles;
            }
        CHECK(tiles == 3 * 3 + 2 * 2 + 1);
    }
    encoder.reset();
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("One focal plane is a bare AV1 stream dav1d decodes: BT.601 full range, 8-bit 4:2:0, the full still-picture header", "[Encode_13]")
{
    const auto tile = in_focus_tile();
    const std::vector<std::span<const std::uint8_t>> planes {tile};
    const auto stream = IrisSlide::encode_av1_tile(planes, QUALITY);
    REQUIRE(stream.size() > 2);
    CHECK(stream[0] == 0x12);   // a temporal delimiter OBU with its size field
    CHECK(stream[1] == 0x00);   // and its empty payload
    const auto obus = obus_of(stream);
    CHECK(obus.front().type == OBU_TEMPORAL_DELIMITER);
    const auto decoded = decode(stream);
    REQUIRE(decoded.size() == 1);
    const auto& sequence = decoded.front().sequence;
    CHECK(sequence.profile == 0);
    CHECK(sequence.hbd == 0);
    CHECK(decoded.front().layout == DAV1D_PIXEL_LAYOUT_I420);
    CHECK(sequence.mtrx == DAV1D_MC_BT601);
    CHECK(sequence.color_range == 1);
    CHECK(sequence.still_picture == 1);
    CHECK(sequence.reduced_still_picture_header == 0);
    CHECK(decoded.front().frame_type == DAV1D_FRAME_TYPE_KEY);
    const auto source = IrisSlide::tile_to_i420(tile);
    CHECK(luma_psnr(decoded.front().luma, std::span(source).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
}

TEST_CASE("An emulated Z-stack is one AV1 stream: the in-focus key frame, then an inter frame per plane", "[Encode_13]")
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
        CHECK(decoded[plane].sequence.color_range == 1);
        const auto source = IrisSlide::tile_to_i420(planes[plane]);
        CHECK(luma_psnr(decoded[plane].luma, std::span(source).first(SIDE * SIDE)) > LUMA_PSNR_FLOOR_DB);
    }
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
