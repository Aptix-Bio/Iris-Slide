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
    # Replaced only inside the repository's dependencies/ folder, or when empty: a directory named
    # from outside (-DIRIS_SLIDE_VCPKG) may hold another project's copy, which this never deletes.
    set(z_dependencies "${iris_slide_bootstrap_root}/dependencies")
    file(GLOB z_existing "${IRIS_SLIDE_VCPKG}/*")
    cmake_path(IS_PREFIX z_dependencies "${IRIS_SLIDE_VCPKG}" NORMALIZE z_inside)
    if(z_existing AND NOT z_inside)
        message(FATAL_ERROR "bootstrap_vcpkg: ${IRIS_SLIDE_VCPKG} is not at the pinned commit and is outside dependencies/; refusing to replace it")
    endif()
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

# Each override's port tree, fetched here, where the network is the caller's: vcpkg reads the
# tree from history at install time with git in a cleaned environment, and on Blacksmith's
# Windows runner that git could not resolve github.com (PR #2's third run). `git archive` reads
# every file of the tree, so the partial clone fetches them all now and vcpkg finds them local.
string(JSON iris_slide_bootstrap_overrides ERROR_VARIABLE iris_slide_bootstrap_none
       GET "${iris_slide_bootstrap_manifest}" overrides)
if(NOT iris_slide_bootstrap_none)
    string(JSON iris_slide_bootstrap_count LENGTH "${iris_slide_bootstrap_overrides}")
    math(EXPR iris_slide_bootstrap_last "${iris_slide_bootstrap_count} - 1")
    foreach(index RANGE ${iris_slide_bootstrap_last})
        string(JSON name GET "${iris_slide_bootstrap_overrides}" ${index} name)
        set(version "")
        foreach(scheme version version-semver version-string version-date)
            string(JSON value ERROR_VARIABLE missing GET "${iris_slide_bootstrap_overrides}" ${index} ${scheme})
            if(NOT missing)
                set(version "${value}")
            endif()
        endforeach()
        string(JSON port_version ERROR_VARIABLE missing GET "${iris_slide_bootstrap_overrides}" ${index} port-version)
        if(missing)
            set(port_version 0)
        endif()
        string(SUBSTRING "${name}" 0 1 initial)
        file(READ "${IRIS_SLIDE_VCPKG}/versions/${initial}-/${name}.json" history)
        string(JSON entries GET "${history}" versions)
        string(JSON entry_count LENGTH "${entries}")
        math(EXPR entry_last "${entry_count} - 1")
        set(tree "")
        foreach(entry RANGE ${entry_last})
            set(entry_version "")
            foreach(scheme version version-semver version-string version-date)
                string(JSON value ERROR_VARIABLE missing GET "${entries}" ${entry} ${scheme})
                if(NOT missing)
                    set(entry_version "${value}")
                endif()
            endforeach()
            string(JSON entry_port ERROR_VARIABLE missing GET "${entries}" ${entry} port-version)
            if(missing)
                set(entry_port 0)
            endif()
            if(entry_version STREQUAL version AND entry_port EQUAL port_version)
                string(JSON tree GET "${entries}" ${entry} git-tree)
                break()
            endif()
        endforeach()
        if(tree STREQUAL "")
            message(FATAL_ERROR "bootstrap_vcpkg: vcpkg's registry has no ${name} ${version}#${port_version} at ${iris_slide_bootstrap_commit}")
        endif()
        iris_slide_bootstrap_run("${IRIS_SLIDE_GIT}" archive --format=tar -o "${IRIS_SLIDE_VCPKG}/override.tar" "${tree}")
        file(REMOVE "${IRIS_SLIDE_VCPKG}/override.tar")
    endforeach()
endif()
