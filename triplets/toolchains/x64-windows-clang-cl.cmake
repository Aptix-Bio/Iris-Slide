# AomCompilerHash_27.20 (RTM Build_05): the toolchain aom alone is built with on Windows, Visual
# Studio's clang-cl (ZStackWindowsCrash_27.19), then vcpkg's own Windows toolchain. Naming the
# compiler in a chainloaded toolchain file makes vcpkg detect it; a configure option reaches the
# port's build only, after the package hash is computed. vcpkg runs a compiler detection of its
# own for each toolchain file and hashes the compiler binary it finds, so aom's package hash
# changes when Visual Studio's clang-cl does (vcpkg-tool 2026-09-26, commands.build.cpp:671, :698,
# :822). vcpkg's scripts/toolchains/windows.cmake, which this file includes, is no longer part of
# aom's hash: a vcpkg pin move that changes its flags leaves a cached aom in place until the
# triplet changes. vcpkg lists a triplet folder's files without recursing (vcpkgpaths.cpp:694),
# so this folder is not read as triplets.
#
# The one place aom's compiler is named; triplets/x64-windows-static-md.cmake chainloads this file
# for aom. The check runs in compiler detection and in the port's configure, both of which see
# vcpkg's Visual Studio environment (VCPKG_LOAD_VCVARS_ENV, set by the triplet).
set(clang_cl "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin/clang-cl.exe")
if(NOT EXISTS "${clang_cl}")
    message(FATAL_ERROR "aom is compiled by clang-cl, and Visual Studio has none at \"${clang_cl}\" "
                        "(VCINSTALLDIR is \"$ENV{VCINSTALLDIR}\"): install its C++ Clang tools for Windows")
endif()
set(CMAKE_C_COMPILER "${clang_cl}")
set(CMAKE_CXX_COMPILER "${clang_cl}")
# CMAKE_TOOLCHAIN_FILE is vcpkg's scripts/buildsystems/vcpkg.cmake in compiler detection, in the
# port's configure and in its try_compile checks, so two levels up is vcpkg's scripts folder.
get_filename_component(vcpkg_scripts_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
get_filename_component(vcpkg_scripts_dir "${vcpkg_scripts_dir}" DIRECTORY)
include("${vcpkg_scripts_dir}/toolchains/windows.cmake")
