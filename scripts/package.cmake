# VcpkgCaching_2 (OneReusableBuild_2.5; RTM Build_03): a build folder's install, with the licence and
# README, as one compressed archive for a release (ArtifactsChecked_17.4 adds the checks on it).
# Usage: cmake -DBUILD=<build folder> -DARCHIVE=<name>.tar.gz -P scripts/package.cmake
cmake_minimum_required(VERSION 3.27)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH root)
cmake_path(ABSOLUTE_PATH BUILD NORMALIZE)
set(stage "${BUILD}/package")
file(REMOVE_RECURSE "${stage}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${stage}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "package: cmake --install ${BUILD} failed (${result})")
endif()
file(COPY "${root}/LICENSE" "${root}/README.md" DESTINATION "${stage}")
file(GLOB contents RELATIVE "${stage}" "${stage}/*")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${ARCHIVE}" -- ${contents}
                WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT EXISTS "${stage}/${ARCHIVE}")
    message(FATAL_ERROR "package: no archive ${ARCHIVE} written (${result})")
endif()
file(RENAME "${stage}/${ARCHIVE}" "${CMAKE_CURRENT_BINARY_DIR}/${ARCHIVE}")
message(STATUS "package: ${ARCHIVE}")
