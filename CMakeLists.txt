cmake_minimum_required(VERSION 3.26)

project(gkNextRenderer LANGUAGES CXX C)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(ENABLE_AVIF "Enable AVIF support" OFF)
if (ENABLE_AVIF)
    set(WITH_AVIF ON CACHE BOOL "Enable AVIF" FORCE)
endif()

option(ENABLE_DLSS "Enable DLSS support" OFF)
if (NOT WIN32)
    if (ENABLE_DLSS)
        message(STATUS "DLSS (Streamline) is only supported on Windows. Disabling.")
    endif()
    set(ENABLE_DLSS OFF CACHE BOOL "Enable DLSS support" FORCE)
endif()

if (ENABLE_DLSS)
    set(WITH_STREAMLINE ON CACHE BOOL "Enable Nvidia Streamline" FORCE)
endif()

option(ENABLE_KTX2 "Enable KTX2 support" ON)
if (ENABLE_KTX2)
    set(WITH_KTX2 ON CACHE BOOL "Enable KTX2" FORCE)
endif()

option(ENABLE_PHYSIC "Enable Physics support" ON)
if (ENABLE_PHYSIC)
    set(WITH_PHYSIC ON CACHE BOOL "Enable Physics" FORCE)
endif()

option(ENABLE_AUDIO "Enable Audio support" OFF)
if (ENABLE_AUDIO)
    set(WITH_AUDIO ON CACHE BOOL "Enable Audio" FORCE)
endif()

option(ENABLE_OIDN "Enable OpenImageDenoise support" OFF)
if (APPLE)
    message(STATUS "OIDN is not supported on macOS. Disabling.")
    set(ENABLE_OIDN OFF CACHE BOOL "Enable OpenImageDenoise support" FORCE)
endif()

if (ENABLE_OIDN)
    set(WITH_OIDN ON CACHE BOOL "Enable OIDN" FORCE)
endif()

option(ENABLE_QUICKJS "Enable QuickJS support" OFF)
if (ENABLE_QUICKJS)
    set(WITH_QUICKJS ON CACHE BOOL "Enable QuickJS" FORCE)
endif()

option(GK_ENABLE_HOT_RELOAD "Enable desktop shader and C++ game plugin hot reload" ON)
if (ANDROID OR IOS)
    if (GK_ENABLE_HOT_RELOAD)
        message(STATUS "Hot reload is only enabled for desktop targets. Disabling.")
    endif()
    set(GK_ENABLE_HOT_RELOAD OFF CACHE BOOL "Enable desktop shader and C++ game plugin hot reload" FORCE)
endif()

option(ENABLE_WHISPERCPP "Enable whisper.cpp support" ON)
if (ANDROID OR IOS)
    if (ENABLE_WHISPERCPP)
        message(STATUS "whisper.cpp is not supported on Android/iOS. Disabling.")
    endif()
    set(ENABLE_WHISPERCPP OFF CACHE BOOL "Enable whisper.cpp support" FORCE)
endif()

if (ENABLE_WHISPERCPP)
    set(WITH_WHISPERCPP ON CACHE BOOL "Enable whisper.cpp" FORCE)
else()
    set(WITH_WHISPERCPP OFF CACHE BOOL "Enable whisper.cpp" FORCE)
endif()

include(cmake/SetupExternalLibs.cmake)
include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})
