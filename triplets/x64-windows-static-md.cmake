# VcpkgCaching_2 (VcpkgManifest_2.1; RTM Build_05): vcpkg's x64-windows-static-md triplet, with two changes.
# - Release builds only: Iris-Slide links release libraries in every configuration, so a
#   debug build of each package would be a second build nothing links.
# - aom is compiled by Visual Studio's clang-cl (ZStackWindowsCrash_27.19). MSVC 19.44 (Visual
#   Studio 2022 17.14, the windows-2025 runners' compiler) miscompiles libaom 3.15.3's
#   motion_mode_rd (av1/encoder/rdopt.c), so an inter-frame encode faults (found 2026-11-28 on CI
#   at ca97e1e). The faulting instruction, read at the crash, is an SSE andps whose memory operand
#   is 8 bytes off the 16-byte alignment it requires. On a Windows x64 machine with MSVC
#   14.44.35207 the two Z-stack budget tests fault as on CI (103/105) and pass with aom from
#   clang-cl (105/105); five CPUs emulated by Intel SDE 10.13.1 pass, AVX-512 included, so the
#   fault follows the compiler (the faulting instruction and its source line are in 880c7ba's
#   message). clang-cl is a component of the same Visual Studio install (C++ Clang tools for
#   Windows), so the runners and a user building with Visual Studio 2022 get the same fix; vcpkg's
#   Windows toolchain already handles it (scripts/toolchains/windows.cmake leaves out /MP for it).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
if(PORT STREQUAL "aom")
    # aom's compiler, clang-cl, is named and checked in the chainloaded toolchain alone, so vcpkg
    # detects it and aom's package hash follows its binary (AomCompilerHash_27.20); a toolchain
    # file turns vcpkg's Visual Studio environment off by default, and clang-cl needs it.
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/toolchains/x64-windows-clang-cl.cmake")
    set(VCPKG_LOAD_VCVARS_ENV ON)
endif()
