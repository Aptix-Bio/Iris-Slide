// What the encoder's tests share (EncoderRewrite_24, Av1TileEncode_27): a scratch folder removed
// with everything in it, and the wait for an encode to leave ACTIVE.
#ifndef IrisSlideTestEncoderSupport_hpp
#define IrisSlideTestEncoderSupport_hpp
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "IrisSlideCore.hpp"

namespace encoder_test {
/// How often a test reads an encoder's progress.
constexpr auto POLL = std::chrono::milliseconds(20);

/// A folder of its own under the temporary directory, removed with everything in it when the
/// test leaves, a failed REQUIRE included.
struct ScratchFolder {
    std::filesystem::path path;
    ScratchFolder()
    {
        path = std::filesystem::temp_directory_path() / ("IrisSlideTest_encoder_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
    }
    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;
    ~ScratchFolder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

/// Waits for @p encoder to leave ACTIVE and returns its last progress.
inline IrisSlide::EncoderProgress wait_for(const IrisSlide::Encoder& encoder)
{
    IrisSlide::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(POLL);
        REQUIRE(IrisSlide::encoder_progress(encoder, progress).succeeded());
    } while (progress.state == IrisSlide::EncoderState::ACTIVE);
    return progress;
}
} // namespace encoder_test
#endif /* IrisSlideTestEncoderSupport_hpp */
