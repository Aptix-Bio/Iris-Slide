# 2025 Copyright Ryan Landvater
# Created 3/3/2025

cmake_minimum_required(VERSION 3.27)
include(FetchContent)
include(ExternalProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(IRIS_BUILD_SHARED "Build IrisSlide Shared Library" ON)
option(IRIS_BUILD_STATIC "Build IrisSlide Static Library" ON)
option(IRIS_BUILD_ENCODER "Build the IrisSlide Encoder executable" ON)
option(IRIS_BUILD_PYTHON "Build IrisSlide Python modules" OFF)
option(IRIS_BUILD_DEPENDENCIES "Build all dependencies and statically link into self-contained binary" OFF)
option(IRIS_USE_OPENSLIDE "Use openslide in the encoder (currently not supported on Windows Arm64)" ON)

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
if (IRIS_BUILD_DEPENDENCIES)
    message(STATUS "Iris Slide is configured to build all dependencies and statically link into a single self-contained binary.")
endif()
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

# Iris-Headers and Iris-File-Extension, from this repository's owner; shared
# with the JavaScript build.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/iris-repositories.cmake)

FetchContent_Declare (
    VulkanHeaders
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${IRIS_DEP_VULKAN_SDK}.tar.gz
)
FetchContent_Declare (
    VkMemoryAllocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/${IRIS_DEP_VMA}.tar.gz
    URL_HASH SHA256=${IRIS_DEP_VMA_SHA256}
)
set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_INSTALL OFF)
set(GLM_BUILD_TESTS   OFF)
FetchContent_Declare (
    GLM
    URL https://github.com/g-truc/glm/archive/refs/tags/${IRIS_DEP_GLM}.tar.gz
    URL_HASH SHA256=${IRIS_DEP_GLM_SHA256}
)
FetchContent_MakeAvailable(
    VulkanHeaders 
    VkMemoryAllocator
    GLM
)

# Threading
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

# Include external projects
include(./cmake/hwy.cmake)
include(./cmake/turbo-jpeg.cmake)
include(./cmake/avif.cmake)
include(./cmake/png.cmake)
include(./cmake/dicom.cmake)
# Include Encoder-required external projects
if (IRIS_BUILD_ENCODER)
    if (IRIS_USE_OPENSLIDE)
        add_compile_definitions(IRIS_INCLUDE_OPENSLIDE=1)
        message(STATUS "BUILDING IRIS SLIDE ENCODER: Encoding from vendor files requires Openslide")
        include(./cmake/openslide.cmake)
        message(STATUS "Found OPENSLIDE_LIB: ${OPENSLIDE_LIB}")
    else ()
        add_compile_definitions(IRIS_INCLUDE_OPENSLIDE=0)
    endif()
    
endif(IRIS_BUILD_ENCODER)
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Iris Slide Universal Object Build
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(IFE_SOURCE_DIR ${irisfileextension_SOURCE_DIR}/include)
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
    ${IFE_SOURCE_DIR}
    # IFE's generated layer: the public umbrella includes IFE_Blocks.hpp and
    # friends from generated_source/, which is not part of the include/
    # layout — it is generated at IFE configure time.
    ${irisfileextension_SOURCE_DIR}/generated_source
    ${glm_SOURCE_DIR}
    ${irisheaders_SOURCE_DIR}/priv
    ${vkmemoryallocator_SOURCE_DIR}/include
    ${vulkanheaders_SOURCE_DIR}/include
)
set (
    IrisSlideDependencies
    ${TURBOJPEG_LIBRARY}
    ${AVIF_LIBRARY}
    ${PNG_LIBRARY}
)
set (
    IrisSlideEncoderDependencies
    ${IrisSlideDependencies}
    ${OPENSLIDE_LIB}
    ${DICOM_LIBRARY}
    Threads::Threads
)
add_library(
    IrisSlideLib OBJECT
    ${IrisSlideSources}
)
target_include_directories (
    IrisSlideLib PRIVATE
    ${IrisSlideInclude}
)
target_compile_definitions (
    IrisSlideLib
    PRIVATE IRIS_EXPORT_API=true 
)
set_target_properties(
    IrisSlideLib 
    PROPERTIES CXX_VISIBILITY_PRESET hidden
)
target_link_libraries (
    IrisSlideLib
    PUBLIC IrisHeaders
)
if (hwy_EXTERNAL_PROJECT_ADD)
    add_dependencies(IrisSlideLib hwy)
endif()
if (TURBOJPEG_EXTERNAL_PROJECT_ADD) 
    add_dependencies(IrisSlideLib TurboJpeg)
endif()
if (AVIF_EXTERNAL_PROJECT_ADD) 
    add_dependencies(IrisSlideLib Avif)
endif()
if (PNG_EXTERNAL_PROJECT_ADD)
    add_dependencies(IrisSlideLib Png)
endif()

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
        $<TARGET_OBJECTS:IrisFileExtensionLib>
        $<TARGET_OBJECTS:IrisSlideLib>
    )
    target_link_libraries(
        IrisSlide
        PRIVATE ${IrisSlideDependencies}
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlide)
endif()

if (IRIS_BUILD_STATIC)
    add_library(
        IrisSlideStatic STATIC 
        $<TARGET_OBJECTS:IrisFileExtensionLib>
        $<TARGET_OBJECTS:IrisSlideLib>
    )
    target_link_libraries(
        IrisSlideStatic
        PRIVATE ${IrisSlideDependencies}
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlideStatic)
endif(IRIS_BUILD_STATIC)

if (IRIS_BUILD_ENCODER AND (IRIS_BUILD_SHARED OR IRIS_BUILD_STATIC))
    add_executable (
        IrisSlideEncoder
        $<TARGET_OBJECTS:IrisFileExtensionLib>
        $<TARGET_OBJECTS:IrisSlideLib>
        ${IrisSlideEncoderSources}
        ${SLIDE_SOURCE_DIR}/EncoderMain.cpp
    )
    target_include_directories(
        IrisSlideEncoder 
        PRIVATE ${IrisSlideInclude}
        PRIVATE ${OPENSLIDE_DIR}
    )
    target_compile_definitions (
        IrisSlideEncoder
        PRIVATE IRIS_EXPORT_API=true 
    )
    target_link_libraries (
        IrisSlideEncoder
        PRIVATE IrisHeaders
        PRIVATE ${IrisSlideEncoderDependencies}
    )
    set(IrisSlideTargets ${IrisSlideTargets} IrisSlideEncoder)
    if (DICOM_EXTERNAL_PROJECT_ADD)
        add_dependencies(IrisSlideEncoder libdicom)
    endif()
endif()

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Installation
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if (${PROJECT_IS_TOP_LEVEL} AND (IRIS_BUILD_SHARED OR IRIS_BUILD_STATIC))
    install(
        TARGETS IrisHeaders
        EXPORT IrisHeadersConfig
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Iris
    )
    install(
        EXPORT IrisHeadersConfig
        FILE IrisHeadersConfig.cmake
        NAMESPACE "Iris::"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Iris
    )
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
    set(PYBIND11_FINDPYTHON ON)
    FetchContent_Declare (
        Pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG "${IRIS_DEP_PYBIND11}"
        GIT_SHALLOW ON
        FETCHCONTENT_QUIET ON
    )
    FetchContent_MakeAvailable (
        Pybind11
    )
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
        $<TARGET_OBJECTS:IrisFileExtensionLib>
        $<TARGET_OBJECTS:IrisSlideLib>
        ${PROJECT_SOURCE_DIR}/python/IrisSlidePython.cpp
    )
    target_include_directories (
        IrisSlidePython 
        PRIVATE ${IrisSlideInclude}
    )
    set_property(
        TARGET IrisSlidePython
        PROPERTY OUTPUT_NAME iris_core
    )
    target_compile_definitions (
        IrisSlidePython
        PRIVATE IRIS_EXPORT_API=true 
    )
    target_link_libraries (
        IrisSlidePython
        PRIVATE IrisHeaders
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
            $<TARGET_OBJECTS:IrisFileExtensionLib>
            $<TARGET_OBJECTS:IrisSlideLib>
            ${IrisSlideEncoderSources}
            ${PROJECT_SOURCE_DIR}/python/IrisEncoderPython.cpp
        )
        target_include_directories (
            IrisSlideEncoderPython 
            PRIVATE ${IrisSlideInclude}
            PRIVATE ${OPENSLIDE_DIR}
        )
        set_property(
            TARGET IrisSlideEncoderPython
            PROPERTY OUTPUT_NAME encoder
        )
        target_compile_definitions (
            IrisSlideEncoderPython
            PRIVATE IRIS_EXPORT_API=true 
        )
        target_link_libraries (
            IrisSlideEncoderPython
            PRIVATE IrisHeaders
            PRIVATE ${IrisSlideEncoderDependencies}
        )
        if (DICOM_EXTERNAL_PROJECT_ADD)
            add_dependencies(IrisSlideEncoderPython libdicom)
        endif()
        install(
            TARGETS IrisSlideEncoderPython
            DESTINATION ${IRIS_INSTALL_PYTHON_LIBDIR}/Iris/Encoder
        )
    endif()
endif()
