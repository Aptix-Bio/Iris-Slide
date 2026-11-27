// RTM Build_07
// OpenSlideOptIn_22.7. Decided 2026-11-27: OpenSlide is an opt-in build option, off by default,
// so the encoder (its program and its Python module) needs no OpenSlide library to encode a DICOM
// or Iris source. A build without it refuses a vendor slide file naming the option that adds it;
// a build with it hands the file to OpenSlide, which refuses one it does not recognise.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlidePriv.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::size_t JUNK_BYTES = 4096;
constexpr char JUNK = 'x';

/// A file named like an Aperio slide that holds no slide, removed at scope exit.
struct VendorNamedFile {
    std::filesystem::path path;
    VendorNamedFile()
    {
        std::random_device entropy;
        path = std::filesystem::temp_directory_path() /
               ("IrisSlideTest_" + std::to_string(std::uint64_t{entropy()} << 32U | entropy()) + ".svs");
        std::ofstream(path, std::ios::binary) << std::string(JUNK_BYTES, JUNK);
    }
    VendorNamedFile(const VendorNamedFile&) = delete;
    VendorNamedFile& operator=(const VendorNamedFile&) = delete;
    ~VendorNamedFile()
    {
        std::error_code ignored;   // nothing to report: the file may already be gone
        std::filesystem::remove(path, ignored);
    }
};
} // namespace

TEST_CASE("A vendor slide file is refused by name, with or without OpenSlide built in", "[Build_07]")
{
    const VendorNamedFile vendor;
    IrisSlide::EncodeSlideInfo info {
        .srcFilePath     = vendor.path.string(),
        .dstFilePath     = vendor.path.parent_path().string(),
        .desiredEncoding = IrisSlide::TILE_ENCODING_JPEG,
    };
    const auto encoder = IrisSlide::create_encoder(info);
    REQUIRE(encoder);
    const auto result = IrisSlide::dispatch_encoder(encoder);
    REQUIRE(result.failed());
#if IRIS_INCLUDE_OPENSLIDE
    CHECK_THAT(result.message, ContainsSubstring("not recognized"));
#else
    CHECK_THAT(result.message, ContainsSubstring("IRIS_USE_OPENSLIDE=ON"));
#endif
}
