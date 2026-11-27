# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): vcpkg's arm64-linux triplet, with one change.
# - Release builds only: Iris-Slide links release libraries in every configuration, so a
#   debug build of each package would be a second build nothing links.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
