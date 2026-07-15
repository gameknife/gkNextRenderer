cmake_minimum_required(VERSION 3.26)

project(gkNextRenderer LANGUAGES CXX C)

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

option(GK_ENABLE_HOT_RELOAD "Enable desktop Slang shader hot reload" ON)
option(GK_ENABLE_SHADER_CLOCK "Enable shader clock heatmap instrumentation" OFF)
if (ANDROID OR IOS)
    if (GK_ENABLE_HOT_RELOAD)
        message(STATUS "Hot reload is only enabled for desktop targets. Disabling.")
    endif()
    set(GK_ENABLE_HOT_RELOAD OFF CACHE BOOL "Enable desktop Slang shader hot reload" FORCE)
endif()

include(cmake/SetupExternalLibs.cmake)
include(cmake/SetupDependencies.cmake)

add_subdirectory(assets)
add_subdirectory(src)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT gkNextRenderer)
