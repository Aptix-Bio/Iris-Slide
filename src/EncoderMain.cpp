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
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "IrisSlideCore.hpp"

namespace {
constexpr char HELP[] =
    "IrisSlideEncoder encodes a whole-slide image as an Iris slide (.iris).\n"
    "  -s, --source PATH        the slide: a DICOM whole-slide instance (JPEG, JPEG 2000 or HTJ2K frames)\n"
    "  -o, --outdir PATH        the folder <source stem>.jpeg.iris is written to (default: the source's)\n"
    "  -q, --quality N          JPEG quality, 1-100 (default 90)\n"
    "      --subsampling S      JPEG chroma subsampling: 444, 422 or 420 (default 422)\n"
    "      --layer-step N       the scale between written layers: 2, 4 or 8 (default 2)\n"
    "      --memory-budget GIB  memory held at once (default: a quarter of physical memory)\n"
    "  -c, --concurrency N      worker threads (default: every core)\n"
    "  -h, --help               this text\n";
constexpr auto PROGRESS_INTERVAL = std::chrono::milliseconds(500);
constexpr double BYTES_PER_GIB = 1024.0 * 1024.0 * 1024.0;
constexpr std::uint64_t BYTES_PER_MIB = 1024 * 1024;
// Exit codes: 0 done, 1 the encode failed, 2 the arguments were wrong.
constexpr int EXIT_DONE = 0, EXIT_ENCODE_FAILED = 1, EXIT_USAGE = 2;

std::optional<std::uint64_t> number_of (std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}
std::optional<IrisSlide::Subsampling> subsampling_of (std::string_view text)
{
    if (text == "444") return IrisSlide::SUBSAMPLE_444;
    if (text == "422") return IrisSlide::SUBSAMPLE_422;
    if (text == "420") return IrisSlide::SUBSAMPLE_420;
    return std::nullopt;
}
int usage (const std::string& why)
{
    std::cerr << "IrisSlideEncoder: " << why << "\n" << HELP;
    return EXIT_USAGE;
}
int encode_failed (const std::string& why)
{
    std::cerr << "\nIrisSlideEncoder: " << why << "\n";
    return EXIT_ENCODE_FAILED;
}
} // namespace

int main (int argc, char const* argv[])
{
    IrisSlide::EncoderDesc desc;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "-h" || flag == "--help") { std::cout << HELP; return EXIT_DONE; }
        if (i + 1 >= argc) return usage(std::string(flag) + " needs a value");
        const std::string_view value = argv[++i];
        if (flag == "-s" || flag == "--source") { desc.source = std::string(value); continue; }
        if (flag == "-o" || flag == "--outdir") { desc.destination = std::string(value); continue; }
        if (flag == "--subsampling") {
            const auto subsampling = subsampling_of(value);
            if (!subsampling) return usage("subsampling \"" + std::string(value) + "\" is not 444, 422 or 420");
            desc.subsampling = *subsampling;
            continue;
        }
        if (flag == "--memory-budget") {
            const double gib = std::strtod(std::string(value).c_str(), nullptr);
            if (!(gib > 0)) return usage("memory budget \"" + std::string(value) + "\" is not a positive number of GiB");
            desc.memory_budget = static_cast<std::uint64_t>(gib * BYTES_PER_GIB);
            continue;
        }
        const auto number = number_of(value);
        if (!number) return usage(std::string(flag) + " takes a number, given \"" + std::string(value) + "\"");
        if (flag == "-q" || flag == "--quality") desc.quality = static_cast<IrisSlide::Quality>(*number);
        else if (flag == "--layer-step") desc.layer_step = static_cast<std::uint32_t>(*number);
        else if (flag == "-c" || flag == "--concurrency") desc.concurrency = static_cast<std::uint32_t>(*number);
        else return usage("unknown argument \"" + std::string(flag) + "\"");
    }
    if (desc.source.empty()) return usage("a source slide is needed (-s)");
    if (desc.destination.empty()) desc.destination = desc.source.has_parent_path() ? desc.source.parent_path() : ".";

    IrisSlide::Encoder encoder;
    if (const auto created = IrisSlide::create_encoder(desc, encoder); created.failed()) return encode_failed(created.message);
    if (const auto started = IrisSlide::dispatch_encoder(encoder); started.failed()) return encode_failed(started.message);

    const auto started = std::chrono::steady_clock::now();
    IrisSlide::EncoderProgress progress;
    for (;;) {
        if (const auto read = IrisSlide::encoder_progress(encoder, progress); read.failed()) return encode_failed(read.message);
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
