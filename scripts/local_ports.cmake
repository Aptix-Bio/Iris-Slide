# VcpkgCaching_2 (DownstreamThroughPorts_2.10; RTM Build_05): build Iris-Slide against a local
# checkout of Iris-Headers or Iris-File-Extension, in place of the commit its port pins. Iris-Headers'
# and IFE's own CI build Iris-Slide against the change under review this way (Iris-Headers'
# downstream.yml), which FETCHCONTENT_SOURCE_DIR_<NAME> did before the ports.
#
# Writes, into OUT, an overlay port for each checkout named: a copy of ports/<name> whose source is
# the checkout instead of the pinned archive. The checkout's commit and the hash of its uncommitted
# changes are written into the copy, so vcpkg's ABI hash, which covers the portfile, changes with
# the source: a cached package built from other source is never restored for it. Configure with
# -DVCPKG_OVERLAY_PORTS=<OUT>, which vcpkg reads before vcpkg-configuration.json's overlays.
#
# Usage: cmake -DOUT=<dir> [-DIRIS_HEADERS=<checkout>] [-DIRIS_FILE_EXTENSION=<checkout>]
#              -P scripts/local_ports.cmake
cmake_minimum_required(VERSION 3.27)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH root)
if(NOT OUT)
    message(FATAL_ERROR "local_ports: name the overlay folder to write, -DOUT=<dir>")
endif()
find_program(GIT git REQUIRED)

function(local_port name checkout)
    cmake_path(ABSOLUTE_PATH checkout NORMALIZE)
    if(NOT EXISTS "${checkout}/CMakeLists.txt")
        message(FATAL_ERROR "local_ports: ${checkout} is not a checkout of ${name} (no CMakeLists.txt)")
    endif()
    execute_process(COMMAND "${GIT}" -C "${checkout}" rev-parse HEAD OUTPUT_VARIABLE commit
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "local_ports: ${checkout} is not a git checkout")
    endif()
    execute_process(COMMAND "${GIT}" -C "${checkout}" diff HEAD OUTPUT_VARIABLE changes)
    string(SHA256 changes_hash "${changes}")

    file(READ "${root}/ports/${name}/portfile.cmake" portfile)
    string(REGEX MATCH "vcpkg_from_github\\([^)]*\\)" pinned "${portfile}")
    if(NOT pinned)
        message(FATAL_ERROR "local_ports: ports/${name}/portfile.cmake has no vcpkg_from_github(...) to replace")
    endif()
    string(REPLACE "${pinned}" "# The local checkout (scripts/local_ports.cmake) at ${commit},\n# its uncommitted changes' SHA-256 ${changes_hash}.\nset(SOURCE_PATH \"${checkout}\")" portfile "${portfile}")
    file(REMOVE_RECURSE "${OUT}/${name}")
    file(COPY "${root}/ports/${name}/" DESTINATION "${OUT}/${name}")
    file(WRITE "${OUT}/${name}/portfile.cmake" "${portfile}")
    message(STATUS "local_ports: ${name} <- ${checkout} at ${commit}")
endfunction()

if(IRIS_HEADERS)
    local_port(iris-headers "${IRIS_HEADERS}")
endif()
if(IRIS_FILE_EXTENSION)
    local_port(iris-file-extension "${IRIS_FILE_EXTENSION}")
endif()
if(NOT IRIS_HEADERS AND NOT IRIS_FILE_EXTENSION)
    message(FATAL_ERROR "local_ports: name a checkout, -DIRIS_HEADERS=<dir> or -DIRIS_FILE_EXTENSION=<dir>")
endif()
