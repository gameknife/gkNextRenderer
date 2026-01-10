cmake_minimum_required(VERSION 3.20)

project(gkNextRenderer)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)

option(GK_ENABLE_AVIF "Enable AVIF support" OFF)
if (GK_ENABLE_AVIF)
    set(WITH_AVIF ON CACHE BOOL "Enable AVIF" FORCE)
endif()

include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})