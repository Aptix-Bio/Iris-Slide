# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is the tip of the
# paired branch feature/Av1TileEncode_27 (Aptix-Bio/Iris-Headers#15), merged before Iris-Slide's:
# EncoderDesc::effort (EncoderEffort_5.7, decided 2026-11-27); then its commit on main
# (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF 9ca5b91746cad2221c737af0671422e85ff8f00d
    SHA512 1799a227080dfc13e9f1e4813408adb5eb0318f448cea7e73d3c7cf88080af228c82aef21287feaa6eac5da5252ff697b0a07fec1df5c39d10ecd544c2844caf
    HEAD_REF feature/Av1TileEncode_27   # main once Aptix-Bio/Iris-Headers#15 merges
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
