# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-File-Extension at a commit, verified by
# the archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is on the
# paired branch feature/BoundaryResults_R-8 (IFE R-8), on which IFE's read calls return a Result
# and write their value through a reference; repinned to IFE main once that pull request merges.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-File-Extension
    REF 56f8c832dbc9224e51073020f33c51721a7dac37
    SHA512 a711bc22b2073be26bdf0dc74e60fcd13a7d7614c2a10d066dfb872bf4b2fbf058d84e0ba52ab752c4b77b6836085d8095520cae570215724ffb4c9b804d21c2
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
