# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is main's merge of
# Aptix-Bio/Iris-Headers#15 (EncoderDesc::effort, decided 2026-11-27), the paired branch that
# merged before Iris-Slide's (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF fb73e7de4636d79a45e1d985fa2862f3e1394a88
    SHA512 1676efce0e7e612d33e23b18f6ffba6bd41a996d191b97aba187cee895d742ee20e743f63b01d9798d156355f3f64dda46933b2d9cd2c7c93aa3b23aa0c13aa7
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
