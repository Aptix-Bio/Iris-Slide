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
    target_sources(IrisSlideTests PRIVATE ${IrisSlideEncoderSources}
                   source_codec_test.cpp openjpeg_frames_test.cpp jpeg_frames_test.cpp
                   local_slides_test.cpp dicom_series_test.cpp
                   retile_test.cpp reduce_test.cpp encoder_rewrite_test.cpp
                   dicom_frames_test.cpp dicom_source_test.cpp)
    set(test_dependencies ${IrisSlideEncoderDependencies})
endif()
iris_slide_compile_settings(IrisSlideTests)
iris_slide_optimise_kernels()
target_link_libraries(
    IrisSlideTests
    PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
    PRIVATE ${test_dependencies} Catch2::Catch2WithMain
)
catch_discover_tests(IrisSlideTests)
# CommandLine_24.13 (RTM Encode_09): the command line refuses what it cannot convert by name,
# before any encode, with exit code 2; each case matches its refusal.
if (IRIS_BUILD_ENCODER)
    set(cli_cases
        "help|--help|encodes a whole-slide image"
        "quality_too_large|-s,absent.dcm,-q,65626|quality 65626 is above 65535"
        "budget_not_a_number|-s,absent.dcm,--memory-budget,2abc|memory budget \"2abc\""
        "subsampling_unknown|-s,absent.dcm,--subsampling,411|subsampling \"411\""
        "argument_unknown|-s,absent.dcm,--colour,red|unknown argument \"--colour\""
        "source_missing|-q,90|a source slide is needed"
        "source_absent|-s,absent.dcm|absent.dcm is not a file")
    foreach(case IN LISTS cli_cases)
        string(REPLACE "|" ";" fields "${case}")
        list(GET fields 0 name)
        list(GET fields 1 arguments)
        list(GET fields 2 expected)
        string(REPLACE "," ";" arguments "${arguments}")
        add_test(NAME cli_${name} COMMAND IrisSlideEncoder ${arguments})
        set_tests_properties(cli_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expected}")
    endforeach()
endif()
# LocalSlidesOptIn_22.10 (decided 2026-11-27): the tests on the maintainer's own slides decode and
# encode whole real slides (a minute or more), so they are hidden from the default run ([.local])
# and run together as one test labelled "local": ctest --preset local.
if (IRIS_BUILD_ENCODER)
    add_test(NAME local_slides COMMAND IrisSlideTests "[local]")
    set_tests_properties(local_slides PROPERTIES LABELS local)
endif()
# RTM Testing_01
# LocalSlideInputs_22.6: the folder of the maintainer's own slides the local-slide
# tests read, decided 2026-11-27. No slide enters the repository; unset, those tests fail by name.
set(IRIS_SLIDE_TEST_SLIDES "" CACHE PATH "Folder of DICOM slides the local-slide tests read")
if (IRIS_SLIDE_TEST_SLIDES)
    target_compile_definitions(IrisSlideTests PRIVATE IRIS_SLIDE_TEST_SLIDES="${IRIS_SLIDE_TEST_SLIDES}")
endif()
