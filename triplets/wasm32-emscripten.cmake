# VcpkgCaching_2 (WasmThroughVcpkg_2.9; RTM Build_05): vcpkg's community wasm32-emscripten triplet,
# reduced to the SDK scripts/bootstrap_emsdk.cmake installs (ENV{EMSDK}, which the toolchain sets),
# and release builds only, as the native triplets.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSDK PATH)
if(NOT DEFINED ENV{EMSDK} OR NOT EXISTS "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    message(FATAL_ERROR "wasm32-emscripten: EMSDK is '$ENV{EMSDK}'; configure through javascript/CMakePresets.json, which installs the pinned SDK")
endif()
set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_BUILD_TYPE release)
# vcpkg's wrapper includes Emscripten.cmake and then applies VCPKG_C(XX)_FLAGS.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/emscripten.cmake")
