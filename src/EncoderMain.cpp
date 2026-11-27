/**
 * @file EncoderMain.cpp
 * @brief IrisSlideEncoder: encode a whole-slide image as an Iris slide (.iris) from the command line.
 *
 * The encoder is Iris-Slide's (ARCHITECTURE D14): it reads a DICOM whole-slide instance (JPEG,
 * JPEG 2000 or HTJ2K frames), derives every lower layer by a 2x bicubic reduction and writes
 * <source stem>.jpeg.iris. This file only parses arguments and reports progress.
 *
 * The Iris Slide Encoder is licensed under the MIT software license.
 */
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "IrisSlideCore.hpp"
#include "IrisSlideEncoderOptions.hpp"

// RTM Encode_09

namespace {
constexpr auto PROGRESS_INTERVAL = std::chrono::milliseconds(500);
constexpr double BYTES_PER_GIB = 1024.0 * 1024.0 * 1024.0;
constexpr std::uint64_t BYTES_PER_MIB = 1024 * 1024;
// Exit codes: 0 done, 1 the encode failed, 2 the arguments were wrong.
constexpr int EXIT_DONE = 0, EXIT_ENCODE_FAILED = 1, EXIT_USAGE = 2;

/// The help text, its defaults read from the encoder's own so they are stated once.
std::string help ()
{
    const IrisSlide::EncoderDesc defaults;
    return std::format(
        "IrisSlideEncoder encodes a whole-slide image as an Iris slide (.iris).\n"
        "  -s, --source PATH        the slide: a DICOM whole-slide instance (JPEG, JPEG 2000 or HTJ2K frames)\n"
        "  -o, --outdir PATH        the folder <source stem>.jpeg.iris is written to (default: the source's)\n"
        "  -q, --quality N          JPEG quality, 1-100 (default {})\n"
        "      --subsampling S      JPEG chroma subsampling: 444, 422 or 420 (default {})\n"
        "      --layer-step N       the scale between written layers, a power of two from 2 (default {})\n"
        "      --memory-budget GIB  memory held at once (default: a quarter of physical memory)\n"
        "  -c, --concurrency N      worker threads (default: every core, {} here)\n"
        "  -h, --help               this text\n",
        defaults.quality, IrisSlide::subsampling_name(defaults.subsampling), defaults.layer_step, defaults.concurrency);
}
/// A whole decimal number; throws std::invalid_argument naming @p flag and the text otherwise.
std::uint64_t number_of (std::string_view flag, std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    const bool is_whole = error == std::errc() && end == text.data() + text.size();
    if (!is_whole) throw std::invalid_argument(std::string(flag) + " takes a whole number, given \"" + std::string(text) + "\"");
    return value;
}
/// A positive number of GiB as bytes; the whole text must be the number.
std::uint64_t gib_of (std::string_view text)
{
    const std::string copy(text);
    char* end = nullptr;
    const double gib = std::strtod(copy.c_str(), &end);
    const bool is_positive = end == copy.c_str() + copy.size() && std::isfinite(gib) && gib > 0;
    if (!is_positive) throw std::invalid_argument("memory budget \"" + copy + "\" is not a positive number of GiB");
    // A double past 2^64 converted to uint64 is undefined behaviour: the largest budget is refused before it.
    constexpr double MAX_BYTES = 18446744073709549568.0;   // the largest double below 2^64
    const double bytes = gib * BYTES_PER_GIB;
    if (bytes > MAX_BYTES) throw std::invalid_argument("memory budget " + copy + " GiB is above 2^64 bytes");
    return static_cast<std::uint64_t>(bytes);
}
int usage (const std::string& why)
{
    std::cerr << "IrisSlideEncoder: " << why << "\n" << help();
    return EXIT_USAGE;
}
int encode_failed (const std::string& why)
{
    std::cerr << "\nIrisSlideEncoder: " << why << "\n";
    return EXIT_ENCODE_FAILED;
}

/// The arguments as an EncoderDesc; throws std::invalid_argument naming the one refused.
IrisSlide::EncoderDesc desc_of (int argc, char const* argv[])
{
    IrisSlide::EncoderDesc desc;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) throw std::invalid_argument(std::string(flag) + " needs a value");
        const std::string_view value = argv[++i];
        if (flag == "-s" || flag == "--source") desc.source = std::string(value);
        else if (flag == "-o" || flag == "--outdir") desc.destination = std::string(value);
        else if (flag == "--subsampling") desc.subsampling = IrisSlide::subsampling_of(value);
        else if (flag == "--memory-budget") desc.memory_budget = gib_of(value);
        else if (flag == "-q" || flag == "--quality")
            desc.quality = IrisSlide::narrowed<IrisSlide::Quality>(number_of(flag, value), "quality");
        else if (flag == "--layer-step")
            desc.layer_step = IrisSlide::narrowed<std::uint32_t>(number_of(flag, value), "layer step");
        else if (flag == "-c" || flag == "--concurrency")
            desc.concurrency = IrisSlide::narrowed<std::uint32_t>(number_of(flag, value), "concurrency");
        else throw std::invalid_argument("unknown argument \"" + std::string(flag) + "\"");
    }
    if (desc.source.empty()) throw std::invalid_argument("a source slide is needed (-s)");
    if (desc.destination.empty()) desc.destination = desc.source.has_parent_path() ? desc.source.parent_path() : ".";
    return desc;
}

int encode (const IrisSlide::EncoderDesc& desc)
{
    IrisSlide::Encoder encoder;
    const auto created = IrisSlide::create_encoder(desc, encoder);
    if (created.failed()) return encode_failed(created.message);
    const auto dispatched = IrisSlide::dispatch_encoder(encoder);
    if (dispatched.failed()) return encode_failed(dispatched.message);

    const auto started = std::chrono::steady_clock::now();
    IrisSlide::EncoderProgress progress;
    for (;;) {
        const auto read = IrisSlide::encoder_progress(encoder, progress);
        if (read.failed()) return encode_failed(read.message);
        if (progress.state != IrisSlide::EncoderState::ACTIVE) break;
        std::cout << "\r" << std::fixed << std::setprecision(1) << progress.fraction * 100.f << "%  "
                  << progress.resident_bytes / BYTES_PER_MIB << " MiB held" << std::flush;
        std::this_thread::sleep_for(PROGRESS_INTERVAL);
    }
    if (progress.state != IrisSlide::EncoderState::DONE) return encode_failed("the encode did not complete: " + progress.error);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "\rWrote " << progress.file.string() << " in " << std::fixed << std::setprecision(1) << seconds << " s\n";
    return EXIT_DONE;
}
} // namespace

int main (int argc, char const* argv[])
{
    // The command line's boundary: every exception becomes a message and a non-zero exit.
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag = argv[i];
            if (flag == "-h" || flag == "--help") { std::cout << help(); return EXIT_DONE; }
        }
        IrisSlide::EncoderDesc desc;
        try {
            desc = desc_of(argc, argv);
        } catch (const std::invalid_argument& refused) {
            return usage(refused.what());
        }
        return encode(desc);
    } catch (const std::exception& error) {
        return encode_failed(error.what());
    } catch (...) {
        return encode_failed("an unknown exception reached main");
    }
}
