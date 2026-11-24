# VcpkgCaching_2 (FirstPartyPorts_2.7; RTM Build_01): Iris-File-Extension at a commit, verified by
# the archive's SHA-512, in place of FetchContent at the tip of a branch. The commit is the tip of
# the paired branch feature/VcpkgCaching_2, which adds IFE_Window.hpp to an Emscripten install and is
# based on feature/PngRetiredClinicalWriter (itself on the rename, X4), until those merge.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Aptix-Bio/Iris-File-Extension
    REF 45845fb3bc42af431588503c856da7281cda70b8
    SHA512 98282851ad8439fc8af69fe8fe407728944a282bbbafe232eabc2faeb3cc2b5d46e6073d60e0d3dd625263b395dc6bf4acede02a1840bf5171d6ceb46011600c
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
