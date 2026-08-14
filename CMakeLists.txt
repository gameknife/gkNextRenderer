cmake_minimum_required(VERSION 3.26)

project(gkNextRenderer LANGUAGES CXX C)

if(IOS)
    enable_language(OBJCXX)
endif()

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(ENABLE_AVIF "Enable AVIF support" OFF)
set(WITH_AVIF ${ENABLE_AVIF})

option(GK_WITH_TUI "Build the optional terminal TUI module" ON)

if (WIN32)
    set(ENABLE_STREAMLINE_DEFAULT ON)
else()
    set(ENABLE_STREAMLINE_DEFAULT OFF)
endif()
option(ENABLE_STREAMLINE "Enable NVIDIA Streamline/DLSS integration" ${ENABLE_STREAMLINE_DEFAULT})
set(WITH_STREAMLINE OFF)
if(WIN32 AND ENABLE_STREAMLINE)
    set(WITH_STREAMLINE ON)
endif()

if (WIN32)
    set(ENABLE_FIDELITYFX_DEFAULT ON)
else()
    set(ENABLE_FIDELITYFX_DEFAULT OFF)
endif()
option(ENABLE_FIDELITYFX "Enable AMD FidelityFX FSR 3.1 Vulkan integration" ${ENABLE_FIDELITYFX_DEFAULT})
set(WITH_FIDELITYFX OFF)
if(WIN32 AND ENABLE_FIDELITYFX)
    set(WITH_FIDELITYFX ON)
endif()

option(GK_ENABLE_HOT_RELOAD "Enable desktop Slang shader hot reload" ON)
option(GK_ENABLE_CPP_LIVE_CODING "Enable Windows C++ live coding through Live++" OFF)
set(GK_LIVEPP_ROOT "$ENV{LIVEPP_ROOT}" CACHE PATH
    "Path to the Live++ root directory (the directory containing API/ and Agent/)")
if(WIN32)
    set(GK_ENABLE_SHADER_CLOCK_DEFAULT ON)
else()
    set(GK_ENABLE_SHADER_CLOCK_DEFAULT OFF)
endif()
option(GK_ENABLE_SHADER_CLOCK "Enable shader clock heatmap instrumentation" ${GK_ENABLE_SHADER_CLOCK_DEFAULT})

if(GK_ENABLE_CPP_LIVE_CODING)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "GK_ENABLE_CPP_LIVE_CODING currently requires 64-bit Windows")
    endif()
    if(ENABLE_LTO)
        message(FATAL_ERROR "Live++ is incompatible with LTO/LTCG; configure with -DENABLE_LTO=OFF")
    endif()

    set(GK_LIVEPP_RESOLVED_ROOT "${GK_LIVEPP_ROOT}")
    if(EXISTS "${GK_LIVEPP_RESOLVED_ROOT}/LivePP/API/x64/LPP_API_x64_CPP.h")
        set(GK_LIVEPP_RESOLVED_ROOT "${GK_LIVEPP_RESOLVED_ROOT}/LivePP")
    endif()
    if(NOT EXISTS "${GK_LIVEPP_RESOLVED_ROOT}/API/x64/LPP_API_x64_CPP.h" OR
       NOT EXISTS "${GK_LIVEPP_RESOLVED_ROOT}/Agent/x64/LPP_Agent_x64_CPP.dll")
        message(FATAL_ERROR
            "Live++ SDK not found. Set GK_LIVEPP_ROOT or LIVEPP_ROOT to either the LivePP directory "
            "or its parent directory. Current value: '${GK_LIVEPP_ROOT}'")
    endif()
    file(TO_CMAKE_PATH "${GK_LIVEPP_RESOLVED_ROOT}" GK_LIVEPP_RESOLVED_ROOT)
    message(STATUS "C++ live coding enabled with Live++: ${GK_LIVEPP_RESOLVED_ROOT}")
endif()

if (ANDROID OR IOS)
    if (GK_ENABLE_HOT_RELOAD)
        message(STATUS "Hot reload is only enabled for desktop targets. Disabling.")
    endif()
    set(GK_ENABLE_HOT_RELOAD OFF CACHE BOOL "Enable desktop Slang shader hot reload" FORCE)
endif()

include(cmake/SetupExternalLibs.cmake)
include(cmake/SetupDotNet.cmake)
include(cmake/SetupDependencies.cmake)

add_subdirectory(assets)
add_subdirectory(src)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT gkNextRenderer)
