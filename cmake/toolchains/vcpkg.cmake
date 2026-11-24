# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): the toolchain file every preset names. It hands the
# configure to vcpkg in manifest mode, which installs vcpkg.json's libraries for the
# build's triplet (restored from the binary cache when built before) before the project
# starts. CMakeLists.txt picks the manifest's features from its options.
#
# The vcpkg is, in order: -DIRIS_SLIDE_VCPKG=<dir>, the VCPKG_ROOT environment variable
# (CI sets it), or dependencies/vcpkg, which scripts/bootstrap_vcpkg.cmake fetches when missing. It must
# be at vcpkg.json's builtin-baseline: a vcpkg at another commit hashes every package
# differently, so the binary cache would miss and rebuild all of them.
cmake_path(ABSOLUTE_PATH CMAKE_CURRENT_LIST_DIR NORMALIZE OUTPUT_VARIABLE iris_slide_toolchains)
cmake_path(GET iris_slide_toolchains PARENT_PATH iris_slide_cmake)
cmake_path(GET iris_slide_cmake PARENT_PATH iris_slide_root)

if(NOT DEFINED IRIS_SLIDE_VCPKG)
    if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(IRIS_SLIDE_VCPKG "$ENV{VCPKG_ROOT}")
    else()
        set(IRIS_SLIDE_VCPKG "${iris_slide_root}/dependencies/vcpkg")
    endif()
endif()
# The compiler checks' own projects read this file again.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES IRIS_SLIDE_VCPKG)
get_property(iris_slide_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if(NOT iris_slide_in_try_compile)
    # Fetches and bootstraps the pinned vcpkg when it is missing or at another commit.
    include("${iris_slide_root}/scripts/bootstrap_vcpkg.cmake")
    file(READ "${iris_slide_root}/vcpkg.json" iris_slide_manifest)
    string(JSON iris_slide_baseline GET "${iris_slide_manifest}" builtin-baseline)
    execute_process(COMMAND git -C "${IRIS_SLIDE_VCPKG}" rev-parse HEAD
                    OUTPUT_VARIABLE iris_slide_vcpkg_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE iris_slide_git_result ERROR_QUIET)
    if(NOT iris_slide_git_result EQUAL 0 OR NOT iris_slide_vcpkg_commit STREQUAL iris_slide_baseline)
        message(FATAL_ERROR "the vcpkg at ${IRIS_SLIDE_VCPKG} is at '${iris_slide_vcpkg_commit}', "
                            "not vcpkg.json's builtin-baseline ${iris_slide_baseline}: run cmake -P scripts/bootstrap_vcpkg.cmake")
    endif()
endif()

# The build machine's triplet, which the native builds also target; Iris-Slide's own
# triplets (triplets/, named in vcpkg-configuration.json) build release libraries only and
# OpenSlide shared.
set(iris_slide_triplet_Darwin_arm64 arm64-osx)
set(iris_slide_triplet_Linux_x86_64 x64-linux)
set(iris_slide_triplet_Linux_aarch64 arm64-linux)
set(iris_slide_triplet_Windows_AMD64 x64-windows-static-md)
set(iris_slide_host_triplet "${iris_slide_triplet_${CMAKE_HOST_SYSTEM_NAME}_${CMAKE_HOST_SYSTEM_PROCESSOR}}")
if(iris_slide_host_triplet STREQUAL "")
    message(FATAL_ERROR "no vcpkg triplet for host ${CMAKE_HOST_SYSTEM_NAME} on ${CMAKE_HOST_SYSTEM_PROCESSOR} (triplets/)")
endif()
set(VCPKG_HOST_TRIPLET "${iris_slide_host_triplet}" CACHE STRING "vcpkg's triplet for tools run during the build")
set(VCPKG_TARGET_TRIPLET "${iris_slide_host_triplet}" CACHE STRING "vcpkg's triplet for the libraries Iris-Slide links")

# Built packages are kept in dependencies/vcpkg-archives beside the vcpkg, instead of
# vcpkg's default in the home directory. CI names its own sources, the GCS store
# (.github/actions/caches).
if("$ENV{VCPKG_BINARY_SOURCES}" STREQUAL "")
    set(ENV{VCPKG_BINARY_SOURCES} "clear;files,${iris_slide_root}/dependencies/vcpkg-archives,readwrite")
    file(MAKE_DIRECTORY "${iris_slide_root}/dependencies/vcpkg-archives")
endif()
set(VCPKG_INSTALL_OPTIONS "--no-print-usage" CACHE STRING "")

include("${IRIS_SLIDE_VCPKG}/scripts/buildsystems/vcpkg.cmake")

# vcpkg rewrites its install log at every configure, so each configure's log is appended
# to a history, which CI's cache report reads (tools/cache_report.py).
if(NOT iris_slide_in_try_compile AND EXISTS "${CMAKE_BINARY_DIR}/vcpkg-manifest-install.log")
    file(READ "${CMAKE_BINARY_DIR}/vcpkg-manifest-install.log" iris_slide_vcpkg_log)
    file(APPEND "${CMAKE_BINARY_DIR}/vcpkg-install-history.log" "${iris_slide_vcpkg_log}")
endif()
