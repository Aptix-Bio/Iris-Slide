# VcpkgCaching_2 (WasmThroughVcpkg_2.9; RTM Build_05): the Emscripten SDK the JavaScript module and
# its vcpkg packages (triplets/wasm32-emscripten.cmake) build with, pinned: emsdk at its release
# tag, verified to be the commit below, with that release installed and activated, in IRIS_SLIDE_EMSDK
# (default: dependencies/emsdk, which git ignores). cmake/toolchains/vcpkg.cmake includes this for
# the wasm32-emscripten triplet, so CI and a local build reach the same SDK through the preset alone.
# Sets ENV{EMSDK}, which the triplet reads, and IRIS_SLIDE_EMSCRIPTEN_TOOLCHAIN, Emscripten's
# CMake toolchain file.
#
# Usage: cmake [-DIRIS_SLIDE_EMSDK=<dir>] -P scripts/bootstrap_emsdk.cmake
if(CMAKE_SCRIPT_MODE_FILE)
    cmake_minimum_required(VERSION 3.27)
endif()
# Release 6.0.13, read 2026-11-24 from https://github.com/emscripten-core/emsdk/releases/tag/6.0.13
set(iris_slide_emsdk_version 6.0.13)
set(iris_slide_emsdk_commit 35659964bc2762fffec0d257997fb61475f765b7)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH iris_slide_emsdk_root)
if(NOT DEFINED IRIS_SLIDE_EMSDK)
    set(IRIS_SLIDE_EMSDK "${iris_slide_emsdk_root}/dependencies/emsdk")
endif()
find_program(IRIS_SLIDE_GIT git REQUIRED)
if(CMAKE_HOST_WIN32)
    set(iris_slide_emsdk_tool "${IRIS_SLIDE_EMSDK}/emsdk.bat")
else()
    set(iris_slide_emsdk_tool "${IRIS_SLIDE_EMSDK}/emsdk")
endif()
set(IRIS_SLIDE_EMSCRIPTEN_TOOLCHAIN "${IRIS_SLIDE_EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

function(iris_slide_emsdk_run)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${IRIS_SLIDE_EMSDK}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "bootstrap_emsdk: `${ARGN}` failed (${result})")
    endif()
endfunction()

execute_process(COMMAND "${IRIS_SLIDE_GIT}" -C "${IRIS_SLIDE_EMSDK}" rev-parse HEAD
                OUTPUT_VARIABLE iris_slide_emsdk_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(EXISTS "${IRIS_SLIDE_EMSCRIPTEN_TOOLCHAIN}" AND iris_slide_emsdk_head STREQUAL iris_slide_emsdk_commit)
    message(STATUS "emsdk ${iris_slide_emsdk_version} already in ${IRIS_SLIDE_EMSDK}")
else()
    message(STATUS "Fetching emsdk ${iris_slide_emsdk_version} into ${IRIS_SLIDE_EMSDK}")
    # Replaced only inside the repository's dependencies/ folder, or when empty: a directory named
    # from outside (-DIRIS_SLIDE_EMSDK) may hold another project's copy, which this never deletes.
    set(z_dependencies "${iris_slide_emsdk_root}/dependencies")
    file(GLOB z_existing "${IRIS_SLIDE_EMSDK}/*")
    cmake_path(IS_PREFIX z_dependencies "${IRIS_SLIDE_EMSDK}" NORMALIZE z_inside)
    if(z_existing AND NOT z_inside)
        message(FATAL_ERROR "bootstrap_emsdk: ${IRIS_SLIDE_EMSDK} is not at the pinned commit and is outside dependencies/; refusing to replace it")
    endif()
    file(REMOVE_RECURSE "${IRIS_SLIDE_EMSDK}")
    file(MAKE_DIRECTORY "${IRIS_SLIDE_EMSDK}")
    iris_slide_emsdk_run("${IRIS_SLIDE_GIT}" init --quiet)
    iris_slide_emsdk_run("${IRIS_SLIDE_GIT}" remote add origin https://github.com/emscripten-core/emsdk)
    iris_slide_emsdk_run("${IRIS_SLIDE_GIT}" fetch --quiet --depth 1 origin "${iris_slide_emsdk_commit}")
    iris_slide_emsdk_run("${IRIS_SLIDE_GIT}" checkout --quiet FETCH_HEAD)
    iris_slide_emsdk_run("${iris_slide_emsdk_tool}" install "${iris_slide_emsdk_version}")
    iris_slide_emsdk_run("${iris_slide_emsdk_tool}" activate "${iris_slide_emsdk_version}")
    if(NOT EXISTS "${IRIS_SLIDE_EMSCRIPTEN_TOOLCHAIN}")
        message(FATAL_ERROR "bootstrap_emsdk: the install left no ${IRIS_SLIDE_EMSCRIPTEN_TOOLCHAIN}")
    endif()
endif()
set(ENV{EMSDK} "${IRIS_SLIDE_EMSDK}")
