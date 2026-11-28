# WindowsCompilers_17.9 (RTM Build_08): puts clang-cl on the job's PATH for the ClangCL cell, so the
# Ninja build finds the compiler CC and CXX name. Visual Studio's own clang-cl is preferred, since
# it is the version that toolset's standard library is tested with; a clang-cl already on PATH is
# taken only when Visual Studio has none.
# Usage: cmake -P scripts/clang_cl_path.cmake, in a job, after the compiler's environment step.
# Needs: GITHUB_PATH (GitHub Actions sets it); VCINSTALLDIR from the compiler's environment, or a
# clang-cl on PATH.
cmake_minimum_required(VERSION 3.27)
if(NOT DEFINED ENV{GITHUB_PATH})
    message(FATAL_ERROR "clang_cl_path: GITHUB_PATH is not set; run it in a GitHub Actions job")
endif()
set(bundled "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin")
if(DEFINED ENV{VCINSTALLDIR} AND EXISTS "${bundled}/clang-cl.exe")
    set(folder "${bundled}")
else()
    find_program(on_path NAMES clang-cl NO_CACHE)
    if(NOT on_path)
        message(FATAL_ERROR "clang_cl_path: no clang-cl in Visual Studio's ${bundled} "
                            "(VCINSTALLDIR is \"$ENV{VCINSTALLDIR}\") nor on PATH")
    endif()
    cmake_path(GET on_path PARENT_PATH folder)
endif()
cmake_path(NATIVE_PATH folder native)
file(APPEND "$ENV{GITHUB_PATH}" "${native}\n")
message(STATUS "clang_cl_path: ${native}")
