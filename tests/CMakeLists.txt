# The library's tests (TestHarness_1; RTM Testing_01). The test executable compiles the
# library's objects with the library's own settings, so a test reaches the private codec
# context (src/IrisSlideContext.hpp) that every tile decode goes through.
find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(
    IrisSlideTests
    $<TARGET_OBJECTS:IrisSlideLib>
    tile_encoding_test.cpp
)
iris_slide_compile_settings(IrisSlideTests)
target_link_libraries(
    IrisSlideTests
    PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
    PRIVATE ${IrisSlideDependencies} Catch2::Catch2WithMain
)
catch_discover_tests(IrisSlideTests)
