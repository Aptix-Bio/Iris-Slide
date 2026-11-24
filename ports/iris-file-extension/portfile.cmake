# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-File-Extension at a commit, verified by
# the archive's SHA-512, in place of FetchContent at the tip of a branch.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-File-Extension
    REF b413498c959ca30e8a9aac626e13029812a57d3e
    SHA512 e2ca73c90b35e943c8bacc5c40ebf32f1d75c7ff44a11e45bf9ddaaf91456b43f8536677955e2466e0e2dbd9f955f582b9e91095518f751bbe660ba97211209b
    HEAD_REF main
)
# IFE generates its block layer from spec/*.json at configure (generator/, standard library only).
vcpkg_find_acquire_program(PYTHON3)
# IFE_EXPORT blank: IFE's objects are compiled into Iris-Slide's binaries, which do not export the
# IFE API (on MSVC, no dllexport or dllimport).
string(APPEND VCPKG_CXX_FLAGS " -DIFE_EXPORT=")
string(APPEND VCPKG_C_FLAGS "")   # vcpkg_cmake_configure takes both or neither
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DIFE_BUILD_SHARED=OFF
        -DIFE_BUILD_STATIC=ON
        -DIFE_BUILD_TESTS=OFF
        -DIFE_BUILD_EXAMPLES=OFF
        "-DPython3_EXECUTABLE=${PYTHON3}"
        # IFE's own Iris-Headers fetch reads the iris-headers port's source tree instead.
        "-DFETCHCONTENT_SOURCE_DIR_IRISHEADERS=${CURRENT_INSTALLED_DIR}/share/IrisHeaders/source"
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME IrisFileExtension CONFIG_PATH lib/cmake/IrisFileExtension)
# The two Iris-Headers headers IFE's install also carries belong to the iris-headers port.
file(REMOVE "${CURRENT_PACKAGES_DIR}/include/Iris/IrisTypes.hpp" "${CURRENT_PACKAGES_DIR}/include/Iris/IrisSlideTypes.hpp")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
