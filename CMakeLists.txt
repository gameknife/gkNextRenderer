cmake_minimum_required(VERSION 3.26)

project(gkNextRenderer LANGUAGES CXX C)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(ENABLE_AVIF "Enable AVIF support" OFF)
if (ENABLE_AVIF)
    set(WITH_AVIF ON CACHE BOOL "Enable AVIF" FORCE)
else()
    set(WITH_AVIF OFF CACHE BOOL "Enable AVIF" FORCE)
endif()

set(WITH_KTX2 ON CACHE BOOL "Enable KTX2" FORCE)
set(WITH_PHYSIC ON CACHE BOOL "Enable Physics" FORCE)
set(WITH_AUDIO ON CACHE BOOL "Enable Audio" FORCE)
set(WITH_QUICKJS ON CACHE BOOL "Enable QuickJS" FORCE)

option(ENABLE_STREAMLINE "Enable NVIDIA Streamline/DLSS integration" OFF)
if (WIN32 AND ENABLE_STREAMLINE)
    set(WITH_STREAMLINE ON CACHE BOOL "Enable NVIDIA Streamline/DLSS integration" FORCE)
else()
    set(WITH_STREAMLINE OFF CACHE BOOL "Enable NVIDIA Streamline/DLSS integration" FORCE)
endif()

option(GK_ENABLE_HOT_RELOAD "Enable desktop Slang shader hot reload" ON)
option(GK_ENABLE_SHADER_CLOCK "Enable shader clock heatmap instrumentation" OFF)
if (ANDROID OR IOS)
    if (GK_ENABLE_HOT_RELOAD)
        message(STATUS "Hot reload is only enabled for desktop targets. Disabling.")
    endif()
    set(GK_ENABLE_HOT_RELOAD OFF CACHE BOOL "Enable desktop Slang shader hot reload" FORCE)
endif()

if (ANDROID OR IOS)
    set(WITH_WHISPERCPP OFF CACHE BOOL "Enable whisper.cpp" FORCE)
else()
    set(WITH_WHISPERCPP ON CACHE BOOL "Enable whisper.cpp" FORCE)
endif()

include(cmake/SetupExternalLibs.cmake)
include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})
