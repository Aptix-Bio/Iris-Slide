# The library's tests (TestHarness_1; RTM Testing_01). The test executable compiles the
# library's objects with the library's own settings, so a test reaches the private codec
# context (src/IrisSlideContext.hpp) that every tile decode goes through. With the encoder
# built, it compiles the encoder's sources too, so a test runs an encode end to end.
find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(
    IrisSlideTests
    $<TARGET_OBJECTS:IrisSlideLib>
    tile_encoding_test.cpp
    ife_read_results_test.cpp
)
set(test_dependencies ${IrisSlideDependencies})
if (IRIS_BUILD_ENCODER)
    target_sources(IrisSlideTests PRIVATE ${IrisSlideEncoderSources} encoder_attributes_test.cpp
                   source_codec_test.cpp openjpeg_frames_test.cpp
                   local_slides_test.cpp dicom_series_test.cpp openslide_optional_test.cpp
                   retile_test.cpp reduce_test.cpp)
    set(test_dependencies ${IrisSlideEncoderDependencies})
endif()
iris_slide_compile_settings(IrisSlideTests)
target_link_libraries(
    IrisSlideTests
    PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
    PRIVATE ${test_dependencies} Catch2::Catch2WithMain
)
catch_discover_tests(IrisSlideTests)
# LocalSlideInputs_22.6 (RTM Testing_01): the folder of the maintainer's own slides the local-slide
# tests read, decided 2026-11-27. No slide enters the repository; unset, those tests fail by name.
set(IRIS_SLIDE_TEST_SLIDES "" CACHE PATH "Folder of DICOM slides the local-slide tests read")
if (IRIS_SLIDE_TEST_SLIDES)
    target_compile_definitions(IrisSlideTests PRIVATE IRIS_SLIDE_TEST_SLIDES="${IRIS_SLIDE_TEST_SLIDES}")
endif()
