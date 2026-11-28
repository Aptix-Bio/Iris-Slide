# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): vcpkg's x64-windows-static-md triplet, with two changes.
# - Release builds only: Iris-Slide links release libraries in every configuration, so a
#   debug build of each package would be a second build nothing links.
# - aom is compiled by Visual Studio's clang-cl (ZStackWindowsCrash_27.19). MSVC 19.44 (Visual
#   Studio 2022 17.14, the windows-2025 runners' compiler) miscompiles libaom 3.15.3's
#   motion_mode_rd (av1/encoder/rdopt.c): an SSE andps reads its sign mask from a stack slot 8
#   bytes off the 16-byte alignment the instruction requires, so an inter-frame encode faults
#   (found 2026-11-27). MSVC 19.51 compiles the same source correctly, so a toolset outside the
#   image would fix the runners but not a user building with Visual Studio 2022; clang-cl from
#   the same Visual Studio install fixes both. vcpkg's Windows toolchain already handles clang-cl.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
if(PORT STREQUAL "aom")
    set(clang_cl "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin/clang-cl.exe")
    if(NOT EXISTS "${clang_cl}")
        message(FATAL_ERROR "aom is compiled by clang-cl, and Visual Studio has none at \"${clang_cl}\" "
                            "(VCINSTALLDIR is \"$ENV{VCINSTALLDIR}\"): install its C++ Clang tools for Windows")
    endif()
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_C_COMPILER=${clang_cl}" "-DCMAKE_CXX_COMPILER=${clang_cl}")
endif()
