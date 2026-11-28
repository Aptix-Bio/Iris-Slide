vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO videolan/dav1d
    REF "${VERSION}"
    SHA512 7ee5906640495919462b2242c44a8c3cc577fdda52fc25257792f6df919429d54e4a48c315a7a11759385f044f68d3d6573fd720853f447e2d8520a13693827f
    HEAD_REF master
)

if (VCPKG_TARGET_ARCHITECTURE STREQUAL "x86" OR VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    vcpkg_find_acquire_program(NASM)
    get_filename_component(NASM_EXE_PATH ${NASM} DIRECTORY)
    vcpkg_add_to_path(${NASM_EXE_PATH})
elseif (VCPKG_TARGET_IS_WINDOWS)
    vcpkg_find_acquire_program(GASPREPROCESSOR)
    foreach(GAS_PATH ${GASPREPROCESSOR})
        get_filename_component(GAS_ITEM_PATH ${GAS_PATH} DIRECTORY)
        vcpkg_add_to_path(${GAS_ITEM_PATH})
    endforeach(GAS_PATH)
endif()

set(LIBRARY_TYPE ${VCPKG_LIBRARY_LINKAGE})
if (LIBRARY_TYPE STREQUAL "dynamic")
    set(LIBRARY_TYPE "shared")
endif(LIBRARY_TYPE STREQUAL "dynamic")

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        --default-library=${LIBRARY_TYPE}
        -Denable_tests=false
        -Denable_tools=false
)

vcpkg_install_meson()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/COPYING"
        "${SOURCE_PATH}/src/ext/x86/x86inc.asm"
)

# Av1TileEncode_27 (RTM Build_05): the one change to vcpkg's dav1d port at the pinned baseline
# (copied above unchanged): dav1d installs a pkg-config file only, so this adds a CMake config with
# an imported target, and Iris-Slide finds every dependency the same way (PIPELINE rule 1),
# find_package(unofficial-dav1d CONFIG REQUIRED) -> unofficial::dav1d::dav1d. A static dav1d
# starts threads and, on Linux, may load symbols, so its target carries Threads and the platform's
# dynamic-loading library.
file(GLOB z_dav1d_archive "${CURRENT_PACKAGES_DIR}/lib/*dav1d*${VCPKG_TARGET_STATIC_LIBRARY_SUFFIX}")
list(LENGTH z_dav1d_archive z_dav1d_count)
if(NOT z_dav1d_count EQUAL 1)
    message(FATAL_ERROR "dav1d: expected one static library in lib/, found '${z_dav1d_archive}'")
endif()
cmake_path(GET z_dav1d_archive FILENAME z_dav1d_name)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/unofficial-dav1d/unofficial-dav1d-config.cmake" "\
include(CMakeFindDependencyMacro)
find_dependency(Threads)
get_filename_component(z_dav1d_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)
if(NOT TARGET unofficial::dav1d::dav1d)
    add_library(unofficial::dav1d::dav1d STATIC IMPORTED)
    set_target_properties(unofficial::dav1d::dav1d PROPERTIES
        IMPORTED_LOCATION \"\${z_dav1d_prefix}/lib/${z_dav1d_name}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${z_dav1d_prefix}/include\"
        INTERFACE_LINK_LIBRARIES \"Threads::Threads;\${CMAKE_DL_LIBS}\")
endif()
unset(z_dav1d_prefix)
")
