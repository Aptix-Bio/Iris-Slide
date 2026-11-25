# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is the tip of
# the paired branch feature/TileEncodingAv1_1 (on main, which holds the merged rename, X4): tile
# encoding 3 is TILE_ENCODING_AV1 in place of TILE_ENCODING_AVIF (decided 2026-11-25, X7), and
# EncodeSlideInfo::attributes carries the caller's attribute pairs (decided 2026-11-25, X3), and
# tile encodings 4 and 5 are TILE_ENCODING_UASTC and TILE_ENCODING_XUASTC (decided 2026-11-25,
# X8), until that branch merges (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF 635afbb5638bee2d29d8024ca4c9418d4ea7c77d
    SHA512 9b735baf2b6144c0987610f5775ef5c1875d9c850e5b82baa97bc042128a4966c3f04370da4cb88b81beeeb3fa4adb23a5640d38a3ee4d8c5ad2d161086c2f4a
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
