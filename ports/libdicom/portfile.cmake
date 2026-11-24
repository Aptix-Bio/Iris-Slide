vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ImagingDataCommons/libdicom
    REF "v${VERSION}"
    SHA512 060c640b8ea4730ec24ed0d576f86ced354af63b752ff893e7327eb715f34a3fbafe223be6e8a76b81ed79014005644aff78aeed100ea5d39b4338d330740cda
    HEAD_REF main
    PATCHES
        cross-build.diff
)
if(VCPKG_CROSSCOMPILING)
    file(COPY
        "${CURRENT_HOST_INSTALLED_DIR}/share/${PORT}/${VERSION}/dicom-dict-lookup.c"
        "${CURRENT_HOST_INSTALLED_DIR}/share/${PORT}/${VERSION}/dicom-dict-lookup.h"
        DESTINATION "${SOURCE_PATH}"
    )
endif()

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dtests=false
)
vcpkg_install_meson()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

vcpkg_copy_tools(TOOL_NAMES dcm-dump dcm-getframe AUTO_CLEAN)

if(NOT VCPKG_CROSSCOMPILING)
    file(COPY
        "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/dicom-dict-lookup.c"
        "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/dicom-dict-lookup.h"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/${VERSION}"
    )
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_05): the one change to vcpkg's libdicom port at
# the pinned baseline (copied above unchanged): libdicom installs a pkg-config file only, so this
# adds a CMake config with an imported target, and Iris-Slide finds every dependency the same way,
# find_package(unofficial-libdicom CONFIG REQUIRED) -> unofficial::libdicom::dicom.
file(GLOB z_libdicom_archive "${CURRENT_PACKAGES_DIR}/lib/*dicom*${VCPKG_TARGET_STATIC_LIBRARY_SUFFIX}")
list(LENGTH z_libdicom_archive z_libdicom_count)
if(NOT z_libdicom_count EQUAL 1)
    message(FATAL_ERROR "libdicom: expected one static library in lib/, found '${z_libdicom_archive}'")
endif()
cmake_path(GET z_libdicom_archive FILENAME z_libdicom_name)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/unofficial-libdicom/unofficial-libdicom-config.cmake" "\
get_filename_component(z_libdicom_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)
if(NOT TARGET unofficial::libdicom::dicom)
    add_library(unofficial::libdicom::dicom STATIC IMPORTED)
    set_target_properties(unofficial::libdicom::dicom PROPERTIES
        IMPORTED_LOCATION \"\${z_libdicom_prefix}/lib/${z_libdicom_name}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${z_libdicom_prefix}/include\")
endif()
unset(z_libdicom_prefix)
")
