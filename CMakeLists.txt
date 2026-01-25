cmake_minimum_required(VERSION 3.20)

project(gkNextRenderer LANGUAGES CXX C)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(GK_ENABLE_AVIF "Enable AVIF support" OFF)
if (GK_ENABLE_AVIF)
    set(WITH_AVIF ON CACHE BOOL "Enable AVIF" FORCE)
endif()

option(GK_ENABLE_DLSS "Enable DLSS support" OFF)
if (NOT WIN32)
    if (GK_ENABLE_DLSS)
        message(STATUS "DLSS (Streamline) is only supported on Windows. Disabling.")
    endif()
    set(GK_ENABLE_DLSS OFF CACHE BOOL "Enable DLSS support" FORCE)
endif()

if (GK_ENABLE_DLSS)
    set(WITH_STREAMLINE ON CACHE BOOL "Enable Nvidia Streamline" FORCE)
endif()

option(GK_ENABLE_KTX2 "Enable KTX2 support" ON)
if (GK_ENABLE_KTX2)
    set(WITH_KTX2 ON CACHE BOOL "Enable KTX2" FORCE)
endif()

option(GK_ENABLE_PHYSIC "Enable Physics support" ON)
if (GK_ENABLE_PHYSIC)
    set(WITH_PHYSIC ON CACHE BOOL "Enable Physics" FORCE)
endif()

option(GK_ENABLE_AUDIO "Enable Audio support" ON)
if (GK_ENABLE_AUDIO)
    set(WITH_AUDIO ON CACHE BOOL "Enable Audio" FORCE)
endif()

option(GK_ENABLE_OIDN "Enable OpenImageDenoise support" OFF)
if (APPLE)
    message(STATUS "OIDN is not supported on macOS. Disabling.")
    set(GK_ENABLE_OIDN OFF CACHE BOOL "Enable OpenImageDenoise support" FORCE)
endif()

if (GK_ENABLE_OIDN)
    set(WITH_OIDN ON CACHE BOOL "Enable OIDN" FORCE)
endif()

option(GK_ENABLE_QUICKJS "Enable QuickJS support" ON)
if (GK_ENABLE_QUICKJS)
    set(WITH_QUICKJS ON CACHE BOOL "Enable QuickJS" FORCE)
endif()

include(cmake/SetupExternalLibs.cmake)
include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})
