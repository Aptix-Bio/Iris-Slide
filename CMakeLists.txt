# 2025 Copyright Ryan Landvater
# Created 3/3/2025

cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(IRIS_BUILD_SHARED "Build IrisSlide Shared Library" ON)
option(IRIS_BUILD_STATIC "Build IrisSlide Static Library" ON)
option(IRIS_BUILD_ENCODER "Build the IrisSlide Encoder executable" ON)
option(IRIS_BUILD_PYTHON "Build IrisSlide Python modules" OFF)
option(IRIS_USE_OPENSLIDE "Use openslide in the encoder (currently not supported on Windows Arm64)" ON)
option(IRIS_SYSTEM_DEPENDENCIES "Find the third-party libraries installed on the system instead of through vcpkg" OFF)

# VcpkgCaching_2 (RTM Build_01, Build_05): every dependency, first-party and third-party, comes
# from vcpkg.json through cmake/toolchains/vcpkg.cmake, prebuilt from vcpkg's binary cache,
# unless another toolchain is named or IRIS_SYSTEM_DEPENDENCIES is on. Nothing is fetched at
# configure. The manifest's features follow the options, and are read when project() loads the
# toolchain, so they are set before it.
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT IRIS_SYSTEM_DEPENDENCIES)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchains/vcpkg.cmake")
endif()
set(VCPKG_MANIFEST_FEATURES "")
if (IRIS_BUILD_ENCODER)
    list(APPEND VCPKG_MANIFEST_FEATURES encoder)
    if (IRIS_USE_OPENSLIDE)
        list(APPEND VCPKG_MANIFEST_FEATURES openslide)
    endif()
endif()
if (IRIS_BUILD_PYTHON)
    list(APPEND VCPKG_MANIFEST_FEATURES python)
endif()

function(get_codec_version)
    set(slide_priv_header "${CMAKE_CURRENT_SOURCE_DIR}/src/IrisSlidePriv.hpp")
    if (NOT EXISTS ${slide_priv_header})
        message(FATAL_ERROR "Could NOT locate Iris Slide Private Header (IrisSlidePriv.hpp)")
    endif()

    file(READ ${slide_priv_header} header)

    if (header MATCHES "#define IRIS_SLIDE_MAJOR_VERSION[ ]*([0-9]+)")
        set(MAJOR_VERSION "${CMAKE_MATCH_1}")
    else () 
        message(FATAL_ERROR "Couldn't parse the Iris Slide major version")
    endif()

    if (header MATCHES "#define IRIS_SLIDE_MINOR_VERSION[ ]*([0-9]+)")
        set(MINOR_VERSION "${CMAKE_MATCH_1}")
    else () 
        message(FATAL_ERROR "Couldn't parse the Iris Slide minor version")
    endif()

    if (header MATCHES "#define IRIS_SLIDE_BUILD_NUMBER[ ]*([0-9]+)")
        set(BUILD_NUMBER "${CMAKE_MATCH_1}")
    else () 
        message(FATAL_ERROR "Couldn't parse the Iris Slide build iteration")
    endif()

    set(IRIS_SLIDE_VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${BUILD_NUMBER}" PARENT_SCOPE)
endfunction()

get_codec_version()
message(STATUS "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~")
message(STATUS "BUILDING IRIS SLIDE COMMUNITY MODULE VERSION v" ${IRIS_SLIDE_VERSION_STRING})
message(STATUS "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~")
if (IRIS_BUILD_PYTHON)
    message(STATUS "Iris Slide is configured to build Python Module")
endif()
if (NOT IRIS_BUILD_ENCODER)
    message(WARNING "Iris Slide is configured with IRIS_BUILD_ENCODER=OFF and will build the DECODER ONLY.")
endif()

PROJECT (
    IrisSlide
    LANGUAGES CXX
    VERSION ${IRIS_SLIDE_VERSION_STRING}
)

# Iris-Headers and Iris-File-Extension (ports/, each pinned by commit and SHA-512). Iris-Slide
# compiles some of Iris-Headers' sources and private headers into its own binaries; the port
# installs them beside its config (ports/iris-headers).
find_package(IrisHeaders CONFIG REQUIRED)
find_package(IrisFileExtension CONFIG REQUIRED)
set(irisheaders_SOURCE_DIR "${IrisHeaders_DIR}/source")

# Threading
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# The third-party libraries (vcpkg.json), each as its imported target. Highway's floor (1.1:
# Iris-Headers' SIMD kernels use SumsOf2 / SumsOf4 / RepartitionToWideX2) is vcpkg.json's.
find_package(hwy CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(libavif CONFIG REQUIRED)
find_package(PNG REQUIRED)
# Iris-Headers' Vulkan types (priv/IrisCoreVulkan.hpp, included by IrisSlideContext.cpp): headers only.
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
set (
    IrisSlideDependencies
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator
    glm::glm-header-only
    hwy::hwy
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg-static>,libjpeg-turbo::turbojpeg-static,libjpeg-turbo::turbojpeg>
    avif
    PNG::PNG
)
set (IrisSlideEncoderDependencies ${IrisSlideDependencies} Threads::Threads)
if (IRIS_BUILD_ENCODER)
    find_package(unofficial-libdicom CONFIG REQUIRED)
    list(APPEND IrisSlideEncoderDependencies unofficial::libdicom::dicom)
    if (IRIS_USE_OPENSLIDE)
        message(STATUS "BUILDING IRIS SLIDE ENCODER: Encoding from vendor files requires Openslide")
        find_package(openslide-bin CONFIG REQUIRED)
        message(STATUS "Found OpenSlide (openslide-bin ${openslide-bin_VERSION})")
        list(APPEND IrisSlideEncoderDependencies OpenSlide::OpenSlide)
    endif()
endif()

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Iris Slide Universal Object Build
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(SLIDE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
set (
    IrisSlideSources
    ${irisheaders_SOURCE_DIR}/src/IrisSIMD.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlideContext.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlideCache.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlide.cpp
)
set (
    IrisSlideEncoderSources
    ${irisheaders_SOURCE_DIR}/src/IrisAsync.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlideDeriveLayers.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlideDcmBridge.cpp
    ${SLIDE_SOURCE_DIR}/IrisSlideEncoder.cpp
)
set (
    IrisSlideInclude
    ${SLIDE_SOURCE_DIR}
    ${irisheaders_SOURCE_DIR}/priv
)
# The compile settings every target that compiles Iris-Slide's sources takes, on the
# target (B7): the include paths; IFE_EXPORT blank, since IFE's static library is linked into
# these binaries and none imports IFE from a DLL; whether the encoder reads OpenSlide.
function(iris_slide_compile_settings target)
    target_include_directories(${target} PRIVATE ${IrisSlideInclude})
    target_compile_definitions(${target} PRIVATE
        IRIS_EXPORT_API=true
        IFE_EXPORT=
        IRIS_INCLUDE_OPENSLIDE=$<BOOL:${IRIS_USE_OPENSLIDE}>)
endfunction()
add_library(
    IrisSlideLib OBJECT
    ${IrisSlideSources}
)
iris_slide_compile_settings(IrisSlideLib)
set_target_properties(
    IrisSlideLib 
    PROPERTIES CXX_VISIBILITY_PRESET hidden
)
target_link_libraries (
    IrisSlideLib
    PUBLIC Iris::IrisHeaders
    PRIVATE ${IrisSlideDependencies} IrisFileExtensionStatic
)

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Iris Slide Targets (ie what we are installing)
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set (
    IrisSlideTargets
    IrisSlideLib
)
if (IRIS_BUILD_SHARED)
    add_library(
        IrisSlide SHARED 
        $<TARGET_OBJECTS:IrisSlideLib>
    )
    target_link_libraries(
        IrisSlide
        PRIVATE ${IrisSlideDependencies} IrisFileExtensionStatic
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlide)
endif()

if (IRIS_BUILD_STATIC)
    add_library(
        IrisSlideStatic STATIC 
        $<TARGET_OBJECTS:IrisSlideLib>
    )
    target_link_libraries(
        IrisSlideStatic
        PRIVATE ${IrisSlideDependencies} IrisFileExtensionStatic
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlideStatic)
endif(IRIS_BUILD_STATIC)

if (IRIS_BUILD_ENCODER AND (IRIS_BUILD_SHARED OR IRIS_BUILD_STATIC))
    add_executable (
        IrisSlideEncoder
        $<TARGET_OBJECTS:IrisSlideLib>
        ${IrisSlideEncoderSources}
        ${SLIDE_SOURCE_DIR}/EncoderMain.cpp
    )
    iris_slide_compile_settings(IrisSlideEncoder)
    target_link_libraries (
        IrisSlideEncoder
        PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
        PRIVATE ${IrisSlideEncoderDependencies}
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlideEncoder)
endif()

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Installation
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if (${PROJECT_IS_TOP_LEVEL} AND (IRIS_BUILD_SHARED OR IRIS_BUILD_STATIC))
    install(
        TARGETS ${IrisSlideTargets}
        EXPORT IrisSlideConfig
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Iris
    )
    install(
        EXPORT IrisSlideConfig
        FILE IrisSlideConfig.cmake
        NAMESPACE "IrisSlide::"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Iris
    )
endif(${PROJECT_IS_TOP_LEVEL} AND (IRIS_BUILD_SHARED OR IRIS_BUILD_STATIC))

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Python support (including installation)
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if(IRIS_BUILD_PYTHON)
    # pybind11 from vcpkg (ports/pybind11: headers and CMake files, no CPython); the modules
    # compile against the interpreter running the build.
    set(PYBIND11_FINDPYTHON ON)
    find_package(pybind11 CONFIG REQUIRED)
    if (NOT CMAKE_INSTALL_PYTHON_LIBDIR)
        set(CMAKE_INSTALL_PYTHON_LIBDIR
        "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages")
    endif ()
    string (REPLACE "\\" "/" IRIS_INSTALL_PYTHON_LIBDIR ${CMAKE_INSTALL_PYTHON_LIBDIR})
    message(STATUS "Installing python modules at: ${IRIS_INSTALL_PYTHON_LIBDIR}")

    # Copy install the python source files
    install(
        DIRECTORY ${PROJECT_SOURCE_DIR}/python/Iris 
        DESTINATION ${IRIS_INSTALL_PYTHON_LIBDIR}
        PATTERN "Encoder" EXCLUDE
    )
    # Then build the Python module
    pybind11_add_module(
        IrisSlidePython
        $<TARGET_OBJECTS:IrisSlideLib>
        ${PROJECT_SOURCE_DIR}/python/IrisSlidePython.cpp
    )
    iris_slide_compile_settings(IrisSlidePython)
    set_property(
        TARGET IrisSlidePython
        PROPERTY OUTPUT_NAME iris_core
    )
    target_link_libraries (
        IrisSlidePython
        PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
        PRIVATE ${IrisSlideDependencies}
    )
    install(
        TARGETS IrisSlidePython
        DESTINATION ${IRIS_INSTALL_PYTHON_LIBDIR}/Iris
    )
    if (IRIS_BUILD_ENCODER)
        install(
            DIRECTORY ${PROJECT_SOURCE_DIR}/python/Iris/Encoder
            DESTINATION ${IRIS_INSTALL_PYTHON_LIBDIR}/Iris/
        )
        pybind11_add_module(
            IrisSlideEncoderPython
                $<TARGET_OBJECTS:IrisSlideLib>
            ${IrisSlideEncoderSources}
            ${PROJECT_SOURCE_DIR}/python/IrisEncoderPython.cpp
        )
        iris_slide_compile_settings(IrisSlideEncoderPython)
        set_property(
            TARGET IrisSlideEncoderPython
            PROPERTY OUTPUT_NAME encoder
        )
        target_link_libraries (
            IrisSlideEncoderPython
            PRIVATE Iris::IrisHeaders IrisFileExtensionStatic
            PRIVATE ${IrisSlideEncoderDependencies}
        )
        install(
            TARGETS IrisSlideEncoderPython
            DESTINATION ${IRIS_INSTALL_PYTHON_LIBDIR}/Iris/Encoder
        )
    endif()
endif()
