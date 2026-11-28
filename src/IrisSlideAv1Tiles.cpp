//
//  IrisSlideAv1Tiles.cpp
//  Iris
//
//  Av1TileEncode_27 (ARCHITECTURE D22, D24): libyuv converts the tile, libaom encodes it. The
//  libaom calls live here alone, so the codec's C API, its status codes and its context's lifetime
//  stay behind the two functions IrisSlideAv1Tiles.hpp declares.
//
#include "IrisSlideAv1Tiles.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include <libyuv/convert_from_argb.h>

#include "IrisSlideEncoderOptions.hpp"

namespace IrisSlide {
namespace {
constexpr std::size_t TILE_PIXELS = std::size_t{RECUT_TILE} * RECUT_TILE;
constexpr std::uint32_t CHROMA_SIDE = RECUT_TILE / 2;   // 4:2:0 halves each axis
/// libaom's timebase: plane n is at time n, one unit long, since a stack carries no timing.
constexpr int TIMEBASE_DENOMINATOR = 1;
constexpr unsigned long FRAME_DURATION = 1;

/// An initialised libaom encoder, destroyed at scope exit.
struct EncoderContext {
    aom_codec_ctx_t codec {};
    bool            is_initialised = false;
    EncoderContext() = default;
    EncoderContext(const EncoderContext&) = delete;
    EncoderContext& operator=(const EncoderContext&) = delete;
    ~EncoderContext() { if (is_initialised) aom_codec_destroy(&codec); }
};

/// Throws naming @p what and libaom's error when @p status is not AOM_CODEC_OK.
void check (const aom_codec_ctx_t& codec, aom_codec_err_t status, const char* what)
{
    if (status == AOM_CODEC_OK) return;
    const char* detail = aom_codec_error_detail(&codec);
    throw std::runtime_error(std::string("libaom: ") + what + " failed: " + aom_codec_err_to_string(status) +
                             (detail ? std::string(" (") + detail + ")" : std::string()));
}

/// Appends every compressed packet @p codec holds to @p stream.
void drain (aom_codec_ctx_t& codec, std::vector<std::uint8_t>& stream)
{
    aom_codec_iter_t iterator = nullptr;
    for (const aom_codec_cx_pkt_t* packet = aom_codec_get_cx_data(&codec, &iterator); packet != nullptr;
         packet = aom_codec_get_cx_data(&codec, &iterator)) {
        if (packet->kind != AOM_CODEC_CX_FRAME_PKT) continue;   // statistics packets carry no stream bytes
        const auto* bytes = static_cast<const std::uint8_t*>(packet->data.frame.buf);
        stream.insert(stream.end(), bytes, bytes + packet->data.frame.sz);
    }
}
} // namespace

// RTM Encode_13
std::vector<std::uint8_t> tile_to_i420 (std::span<const std::uint8_t> rgba)
{
    if (rgba.size() != TILE_PIXELS * RGBA_BYTES)
        throw std::invalid_argument("an AV1 tile takes " + std::to_string(TILE_PIXELS * RGBA_BYTES) + " RGBA bytes; this one has " +
                                    std::to_string(rgba.size()));
    std::vector<std::uint8_t> i420(I420_TILE_BYTES);
    std::uint8_t* y = i420.data();
    std::uint8_t* u = y + TILE_PIXELS;
    std::uint8_t* v = u + std::size_t{CHROMA_SIDE} * CHROMA_SIDE;
    const int converted = libyuv::ABGRToJ420(rgba.data(), int(RECUT_TILE * RGBA_BYTES), y, int(RECUT_TILE), u, int(CHROMA_SIDE),
                                             v, int(CHROMA_SIDE), int(RECUT_TILE), int(RECUT_TILE));
    if (converted != 0) throw std::runtime_error("libyuv: ABGRToJ420 failed (" + std::to_string(converted) + ")");
    return i420;
}

// RTM Encode_13
std::vector<std::uint8_t> encode_av1_tile (std::span<const std::span<const std::uint8_t>> planes, std::uint32_t quality,
                                           Effort effort)
{
    if (planes.empty()) throw std::invalid_argument("an AV1 tile needs at least one focal plane");
    if (quality < std::uint32_t{QUALITY_MIN} || quality > std::uint32_t{QUALITY_MAX})
        throw std::invalid_argument("AV1 quality " + std::to_string(quality) + " is outside " + std::to_string(QUALITY_MIN) +
                                    "-" + std::to_string(QUALITY_MAX));
    if (effort > EFFORT_MAX)
        throw std::invalid_argument("AV1 effort " + std::to_string(int(effort)) + " is outside " + std::to_string(int(EFFORT_MIN)) +
                                    "-" + std::to_string(int(EFFORT_MAX)));
    std::vector<std::vector<std::uint8_t>> i420;
    i420.reserve(planes.size());
    for (const auto plane : planes) i420.push_back(tile_to_i420(plane));

    aom_codec_iface_t* av1 = aom_codec_av1_cx();
    aom_codec_enc_cfg_t config {};
    EncoderContext context;
    check(context.codec, aom_codec_enc_config_default(av1, &config, AOM_USAGE_GOOD_QUALITY), "aom_codec_enc_config_default");
    config.g_w = RECUT_TILE;
    config.g_h = RECUT_TILE;
    config.g_profile = AV1_PROFILE_MAIN;
    config.g_bit_depth = AOM_BITS_8;
    config.g_input_bit_depth = AV1_BIT_DEPTH;
    config.g_threads = 1;                    // one thread per tile: the same bytes at any concurrency (D24)
    config.g_lag_in_frames = 0;              // no look-ahead: each plane is emitted as it is given
    config.g_limit = static_cast<unsigned int>(planes.size());   // one plane makes a still picture's header
    config.full_still_picture_hdr = 1;       // that header in full, which every decoder reads (D22)
    config.kf_max_dist = config.g_limit;     // one key frame: the in-focus plane opens the stack
    config.g_timebase = {1, TIMEBASE_DENOMINATOR};
    config.rc_end_usage = AOM_Q;             // constant quantiser: quality alone sets the size
    // Quality 100 is the finest quantiser (0) and quality 1 the coarsest (AV1_QUANTISER_MAX).
    const std::uint32_t quantiser = (std::uint32_t(QUALITY_MAX) - quality) * AV1_QUANTISER_MAX / std::uint32_t(QUALITY_MAX - QUALITY_MIN);
    // Every plane at that quantiser, the key frame included: without the range pinned, libaom's
    // rate control gives a key frame that later planes reference a finer one (found 2026-11-27: a
    // five-plane tissue tile at quality 90 had its key frame at q-index 2 and the rest at 24, 2.63
    // times the in-focus plane's bytes alone).
    config.rc_min_quantizer = quantiser;
    config.rc_max_quantizer = quantiser;
    check(context.codec, aom_codec_enc_init(&context.codec, av1, &config, 0), "aom_codec_enc_init");
    context.is_initialised = true;

    check(context.codec, aom_codec_control(&context.codec, AOME_SET_CQ_LEVEL, static_cast<int>(quantiser)), "AOME_SET_CQ_LEVEL");
    check(context.codec, aom_codec_control(&context.codec, AOME_SET_CPUUSED, AV1_FASTEST_CPU_USED - int(effort)), "AOME_SET_CPUUSED");
    check(context.codec, aom_codec_control(&context.codec, AV1E_SET_ROW_MT, 0), "AV1E_SET_ROW_MT");
    check(context.codec, aom_codec_control(&context.codec, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE), "AV1E_SET_COLOR_RANGE");
    check(context.codec, aom_codec_control(&context.codec, AV1E_SET_MATRIX_COEFFICIENTS, AOM_CICP_MC_BT_601),
          "AV1E_SET_MATRIX_COEFFICIENTS");

    std::vector<std::uint8_t> stream;
    for (std::size_t plane = 0; plane < i420.size(); ++plane) {
        aom_image_t image {};
        if (aom_img_wrap(&image, AOM_IMG_FMT_I420, RECUT_TILE, RECUT_TILE, 1, i420[plane].data()) == nullptr)
            throw std::runtime_error("libaom: aom_img_wrap refused plane " + std::to_string(plane));
        image.range = AOM_CR_FULL_RANGE;
        image.mc = AOM_CICP_MC_BT_601;
        check(context.codec, aom_codec_encode(&context.codec, &image, aom_codec_pts_t(plane), FRAME_DURATION, 0), "aom_codec_encode");
        drain(context.codec, stream);
    }
    check(context.codec, aom_codec_encode(&context.codec, nullptr, 0, FRAME_DURATION, 0), "aom_codec_encode (flush)");
    drain(context.codec, stream);
    if (stream.empty()) throw std::runtime_error("libaom: the encode of " + std::to_string(planes.size()) + " AV1 plane(s) wrote no bytes");
    return stream;
}
} // namespace IrisSlide
