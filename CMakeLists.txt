cmake_minimum_required(VERSION 3.20)

project(gkNextRenderer)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(GK_ENABLE_AVIF "Enable AVIF support" OFF)
if (GK_ENABLE_AVIF)
    set(WITH_AVIF ON CACHE BOOL "Enable AVIF" FORCE)
endif()

option(GK_ENABLE_DLSS "Enable DLSS support" OFF)
if (GK_ENABLE_DLSS)
    set(WITH_STREAMLINE ON CACHE BOOL "Enable Nvidia Streamline" FORCE)
endif()

option(GK_ENABLE_OIDN "Enable OpenImageDenoise support" OFF)
if (APPLE)
    message(STATUS "OIDN is not supported on macOS. Disabling.")
    set(GK_ENABLE_OIDN OFF CACHE BOOL "Enable OpenImageDenoise support" FORCE)
endif()

if (GK_ENABLE_OIDN)
    set(WITH_OIDN ON CACHE BOOL "Enable OIDN" FORCE)
endif()

include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})