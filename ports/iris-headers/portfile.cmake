# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-Headers at a commit, verified by the
# archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is on main: the
# merge of Aptix-Bio/Iris-Headers#13, which carries the rebuilt encoder's public API
# (create_encoder(const EncoderDesc&, Encoder&), dispatch_encoder, encoder_progress,
# interrupt_encoder, encoder_output_name, LAYER_STEP_DEFAULT; decided 2026-11-27, D14)
# (PortsPinMain_17.6).
set(VCPKG_BUILD_TYPE release)   # headers and sources only: nothing is compiled here
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-Headers
    REF 4eecc967cd173529a941487513b67efdfb2a4332
    SHA512 69d5b0ff0a7967402b827108fe6cc5dfa0113c336ee1673b4a485fa019b14005780e57d77ba54f6ed177dbdf0846fe508d2d2b45572268aee1add57a3e3753c7
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
