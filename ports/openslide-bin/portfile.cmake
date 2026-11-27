# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_05): OpenSlide 4.0.1 as OpenSlide's own prebuilt
# release, openslide-bin 4.0.1.2 (the version Iris-Headers pins), each archive verified by its
# SHA-512 (the SHA-256 GitHub lists for each asset matched, read 2026-11-24). vcpkg's openslide port
# builds OpenSlide with glib and cairo, and one of their build tools (gperf) needs autoconf,
# automake and libtool installed on the system (measured on macOS, 2026-11-24); the prebuilt library
# needs nothing, and is the one the encoder linked before. A prebuilt dependency is taken over one
# built from source, since a build on a runner spends billed minutes on every cache miss.
# OpenSlide is LGPL-2.1 and stays a shared library, linked dynamically.
set(VCPKG_BUILD_TYPE release)   # openslide-bin ships one build
set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled)   # the macOS archive is universal (arm64, x86_64)

set(z_openslide_version "${VERSION}")
if(VCPKG_TARGET_IS_WINDOWS)
    set(z_openslide_platform windows-x64)
    set(z_openslide_extension zip)
    set(z_openslide_sha512 d80d1c749c04e28fdfd092335995660da92d11ceb3009e0e8aa5e6595bdd8f1e807962ce167621096d0bec46616fa4f855118bafb1402c8600d749892dbbae5b)
elseif(VCPKG_TARGET_IS_OSX)
    set(z_openslide_platform macos-arm64-x86_64)
    set(z_openslide_extension tar.xz)
    set(z_openslide_sha512 efc566f3dba6e753e44a6a1333d058a596c047ef52734a71dc83c5c061ae3f2e0dbfbd58517f86fc2fcc7b8d6ca7fbe212b1b4f80e8539ef2c0a5af0d89970c0)
elseif(VCPKG_TARGET_IS_LINUX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(z_openslide_platform linux-x86_64)
    set(z_openslide_extension tar.xz)
    set(z_openslide_sha512 47092cadec2f8619e0840839f23f74046303d4cff7d0976c0ada0049b2524323b5af0027d591f367eeb577d800a50eb4161d7f1c950d58ac1f99a361c82e8d63)
elseif(VCPKG_TARGET_IS_LINUX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(z_openslide_platform linux-aarch64)
    set(z_openslide_extension tar.xz)
    set(z_openslide_sha512 6044ce8c8d9b045d19e48c4479d72e02a0c11d8e5263eaf2f897b7e6fc6f1c995b4bb87e88a6ed8f9e761c45740bc3b1c8003032bbb2bde0d3e858f5eab3b20e)
else()
    message(FATAL_ERROR "openslide-bin ${z_openslide_version} has no build for ${TARGET_TRIPLET}")
endif()
set(z_openslide_name "openslide-bin-${z_openslide_version}-${z_openslide_platform}")
vcpkg_download_distfile(z_openslide_archive
    URLS "https://github.com/openslide/openslide-bin/releases/download/v${z_openslide_version}/${z_openslide_name}.${z_openslide_extension}"
    FILENAME "${z_openslide_name}.${z_openslide_extension}"
    SHA512 ${z_openslide_sha512}
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${z_openslide_archive}")

file(INSTALL "${SOURCE_PATH}/include/openslide" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${SOURCE_PATH}/bin/libopenslide-1.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${SOURCE_PATH}/lib/libopenslide.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    set(z_openslide_location "bin/libopenslide-1.dll")
    set(z_openslide_implib "\n        IMPORTED_IMPLIB \"\${z_openslide_prefix}/lib/libopenslide.lib\"")
elseif(VCPKG_TARGET_IS_OSX)
    file(INSTALL "${SOURCE_PATH}/lib/libopenslide.1.dylib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    set(z_openslide_location "lib/libopenslide.1.dylib")
else()
    file(GLOB z_openslide_libraries "${SOURCE_PATH}/lib/libopenslide.so*")
    list(FILTER z_openslide_libraries EXCLUDE REGEX "\\.debug$")
    file(INSTALL ${z_openslide_libraries} DESTINATION "${CURRENT_PACKAGES_DIR}/lib" FOLLOW_SYMLINK_CHAIN)
    set(z_openslide_location "lib/libopenslide.so.1")
endif()

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}-config.cmake" "\
get_filename_component(z_openslide_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)
if(NOT TARGET OpenSlide::OpenSlide)
    add_library(OpenSlide::OpenSlide SHARED IMPORTED)
    set_target_properties(OpenSlide::OpenSlide PROPERTIES
        IMPORTED_LOCATION \"\${z_openslide_prefix}/${z_openslide_location}\"${z_openslide_implib}
        INTERFACE_INCLUDE_DIRECTORIES \"\${z_openslide_prefix}/include\")
endif()
unset(z_openslide_prefix)
")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}-config-version.cmake" "\
set(PACKAGE_VERSION ${z_openslide_version})
set(PACKAGE_VERSION_COMPATIBLE TRUE)
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
endif()
")
# The licenses of OpenSlide and of every library built into it.
file(GLOB z_openslide_licenses "${SOURCE_PATH}/licenses/*/*")
vcpkg_install_copyright(FILE_LIST ${z_openslide_licenses})
