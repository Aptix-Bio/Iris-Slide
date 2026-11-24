# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): vcpkg at the commit vcpkg.json's
# builtin-baseline names, bootstrapped, in IRIS_SLIDE_VCPKG (default: dependencies/vcpkg, which git
# ignores). cmake/toolchains/vcpkg.cmake includes this when its vcpkg is missing, so a local
# configure and CI reach the same vcpkg through the preset alone, on every operating system.
#
# vcpkg.json's version overrides are read from old port trees in vcpkg's history, which a shallow
# clone lacks ("vcpkg was cloned as a shallow repository", 2026-11-24), so the commit is fetched as
# a treeless partial clone: every commit, and each tree and file only when git first reads it
# (9 s and 30 MB here, against a whole clone's hundreds of megabytes). A directory already at the
# commit with its tool built is left alone.
#
# Usage: cmake [-DIRIS_SLIDE_VCPKG=<dir>] -P scripts/bootstrap_vcpkg.cmake
if(CMAKE_SCRIPT_MODE_FILE)
    cmake_minimum_required(VERSION 3.27)
endif()
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH iris_slide_bootstrap_root)
if(NOT DEFINED IRIS_SLIDE_VCPKG)
    set(IRIS_SLIDE_VCPKG "${iris_slide_bootstrap_root}/dependencies/vcpkg")
endif()
file(READ "${iris_slide_bootstrap_root}/vcpkg.json" iris_slide_bootstrap_manifest)
string(JSON iris_slide_bootstrap_commit GET "${iris_slide_bootstrap_manifest}" builtin-baseline)
if(NOT iris_slide_bootstrap_commit MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "vcpkg.json's builtin-baseline is '${iris_slide_bootstrap_commit}', not a commit SHA")
endif()
find_program(IRIS_SLIDE_GIT git REQUIRED)
if(CMAKE_HOST_WIN32)
    set(iris_slide_bootstrap_tool "${IRIS_SLIDE_VCPKG}/vcpkg.exe")
    set(iris_slide_bootstrap_script "${IRIS_SLIDE_VCPKG}/bootstrap-vcpkg.bat")
else()
    set(iris_slide_bootstrap_tool "${IRIS_SLIDE_VCPKG}/vcpkg")
    set(iris_slide_bootstrap_script "${IRIS_SLIDE_VCPKG}/bootstrap-vcpkg.sh")
endif()

function(iris_slide_bootstrap_run)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${IRIS_SLIDE_VCPKG}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "bootstrap_vcpkg: `${ARGN}` failed (${result})")
    endif()
endfunction()

execute_process(COMMAND "${IRIS_SLIDE_GIT}" -C "${IRIS_SLIDE_VCPKG}" rev-parse HEAD
                OUTPUT_VARIABLE iris_slide_bootstrap_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(EXISTS "${iris_slide_bootstrap_tool}" AND iris_slide_bootstrap_head STREQUAL iris_slide_bootstrap_commit)
    message(STATUS "vcpkg ${iris_slide_bootstrap_commit} already in ${IRIS_SLIDE_VCPKG}")
else()
    message(STATUS "Fetching vcpkg ${iris_slide_bootstrap_commit} into ${IRIS_SLIDE_VCPKG}")
    file(REMOVE_RECURSE "${IRIS_SLIDE_VCPKG}")
    file(MAKE_DIRECTORY "${IRIS_SLIDE_VCPKG}")
    iris_slide_bootstrap_run("${IRIS_SLIDE_GIT}" init --quiet)
    iris_slide_bootstrap_run("${IRIS_SLIDE_GIT}" remote add origin https://github.com/microsoft/vcpkg)
    iris_slide_bootstrap_run("${IRIS_SLIDE_GIT}" fetch --quiet --filter=tree:0 origin "${iris_slide_bootstrap_commit}")
    iris_slide_bootstrap_run("${IRIS_SLIDE_GIT}" checkout --quiet FETCH_HEAD)
    iris_slide_bootstrap_run("${iris_slide_bootstrap_script}" -disableMetrics)
    if(NOT EXISTS "${iris_slide_bootstrap_tool}")
        message(FATAL_ERROR "bootstrap_vcpkg: the bootstrap left no ${iris_slide_bootstrap_tool}")
    endif()
endif()
