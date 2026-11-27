# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is the tip of the
# paired branch feature/EncoderRewrite_24, merged before Iris-Slide's: the rebuilt encoder's public
# API (create_encoder(const EncoderDesc&, Encoder&), dispatch_encoder, encoder_progress,
# interrupt_encoder, encoder_output_name, LAYER_STEP_DEFAULT) in place of the old one (decided 2026-11-27, D14); then
# its commit on main (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF 2cfb551c5d5a4a931c98a26ae1a377268dbe969a
    SHA512 b146223e674710f653df0ea39e7f3527b34eae624addf48c997fc7bacfeddea3cf8aeef708bf247d9cbd03cf079aa0240c255f3a7b6de1206cbb2356949f7fd9
    HEAD_REF feature/EncoderRewrite_24   # main once Aptix-Bio/Iris-Headers#13 merges
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
