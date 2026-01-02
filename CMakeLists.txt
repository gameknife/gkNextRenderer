cmake_minimum_required(VERSION 3.10)

project(gkNextRenderer)

include(cmake/SetupPlatform.cmake)
include(cmake/ProjectOptions.cmake)
include(cmake/SetupDependencies.cmake)

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})