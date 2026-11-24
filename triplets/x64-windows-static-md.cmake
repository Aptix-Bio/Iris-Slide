# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): vcpkg's x64-windows-static-md triplet, with two changes.
# - Release builds only: Iris-Slide links release libraries in every configuration, so a
#   debug build of each package would be a second build nothing links.
# - OpenSlide (LGPL-2.1; ports/openslide-bin, OpenSlide's prebuilt library) is a shared
#   library, linked dynamically by the encoder and the Python modules.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
if(PORT STREQUAL "openslide-bin")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
