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
                   dicom_frames_test.cpp dicom_source_test.cpp av1_tiles_test.cpp)
    # RTM Encode_13: dav1d decodes the encoder's AV1 tiles back (ARCHITECTURE D22). vcpkg's dav1d
    # ships a pkg-config file and no CMake package, so its header and library are found directly.
    find_path(DAV1D_INCLUDE_DIR dav1d/dav1d.h REQUIRED)
    find_library(DAV1D_LIBRARY NAMES dav1d libdav1d REQUIRED)
    add_library(dav1d::dav1d UNKNOWN IMPORTED)
    set_target_properties(dav1d::dav1d PROPERTIES IMPORTED_LOCATION "${DAV1D_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${DAV1D_INCLUDE_DIR}")
    set(test_dependencies ${IrisSlideEncoderDependencies} dav1d::dav1d)
endif()
iris_slide_compile_settings(IrisSlideTests)
iris_slide_optimise_kernels()
target_link_libraries(
    IrisSlideTests
    PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
    PRIVATE ${test_dependencies} Catch2::Catch2WithMain
)
catch_discover_tests(IrisSlideTests)
# RTM Encode_09
# CommandLine_24.13: the command line refuses what it cannot convert by name, before any encode:
# exit code 2 for an argument, 1 for an encode that fails, 0 for --help; each case checks the
# code and the message (cli_case.cmake).
if (IRIS_BUILD_ENCODER)
    set(cli_cases
        "help|--help|0|encodes a whole-slide image"
        "quality_too_large|-s,absent.dcm,-q,65626|2|quality 65626 is above 65535"
        "budget_not_a_number|-s,absent.dcm,--memory-budget,2abc|2|memory budget \"2abc\""
        "budget_too_large|-s,absent.dcm,--memory-budget,1e30|2|is above 2\\^64 bytes"
        "subsampling_unknown|-s,absent.dcm,--subsampling,411|2|subsampling \"411\""
        "encoding_unknown|-s,absent.dcm,--encoding,webp|2|encoding \"webp\""
        "av1_chroma_refused|-s,absent.dcm,-e,av1,--subsampling,444|1|AV1 tiles are 4:2:0"
        "argument_unknown|-s,absent.dcm,--colour,red|2|unknown argument \"--colour\""
        "source_missing|-q,90|2|a source slide is needed"
        "source_absent|-s,absent.dcm|1|absent.dcm is not a file")
    foreach(case IN LISTS cli_cases)
        string(REPLACE "|" ";" fields "${case}")
        list(GET fields 0 name)
        list(GET fields 1 arguments)
        list(GET fields 2 code)
        list(GET fields 3 expected)
        add_test(NAME cli_${name}
                 COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:IrisSlideEncoder> -DARGUMENTS=${arguments}
                         -DCODE=${code} -DEXPECT=${expected} -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_case.cmake)
    endforeach()
endif()
# RTM Testing_01
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
