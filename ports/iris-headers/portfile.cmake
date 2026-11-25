# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is the tip of
# the paired branch feature/TileEncodingAv1_1 (on main, which holds the merged rename, X4): tile
# encoding 3 is TILE_ENCODING_AV1 in place of TILE_ENCODING_AVIF (decided 2026-11-25, X7), until
# that branch merges (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF 4c78915a2627c18113047f405d2f56d16edfbe59
    SHA512 159c0214a1a4d16a340a5a2451e12b69625d4a88b9f12115a0f69a220e68b89353e7ec2128a5ed64266949713a2c82326ae3aef05cf00473859e1b9da46d3a96
    HEAD_REF main
)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS -DIRIS_HEADERS_CHECK_STANDALONE=OFF)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME IrisHeaders CONFIG_PATH lib/cmake/Iris)
# Iris-Headers' install carries its public headers only. Iris-Slide and Iris-File-Extension
# also compile its private headers and sources (priv/, src/) into their own binaries, so the
# source tree goes beside the package's config, where both find it: ${IrisHeaders_DIR}/source.
file(INSTALL "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/share/IrisHeaders/source"
     PATTERN ".git" EXCLUDE PATTERN ".github" EXCLUDE)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
