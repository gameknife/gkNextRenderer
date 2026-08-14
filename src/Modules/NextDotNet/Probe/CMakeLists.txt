# Standalone build for the Phase 0 acceptance probe.
#
# Kept out of the engine build on purpose: the probe exists to answer whether the two-backend route
# works at all, and it has to configure in seconds without vcpkg, Vulkan or a window. Phase 3 wires
# the same Host/ sources into the real NextDotNet module; nothing here is on that path.
#
# Driven by 'gnb dotnet probe', which fills in the cache variables below.

cmake_minimum_required(VERSION 3.21)
project(gkNextDotNetProbe CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GK_DOTNET_BACKEND "CoreCLR" CACHE STRING "Managed backend: CoreCLR or AOT")
set(GK_DOTNET_HOSTPACK "" CACHE PATH "Directory with hostfxr.h and coreclr_delegates.h (CoreCLR only)")
set(GK_DOTNET_AOT_LIB "" CACHE FILEPATH "Import/static library produced by 'dotnet publish -p:PublishAot=true' (AOT only)")

get_filename_component(GK_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_executable(gkNextDotNetProbe
    DotNetProbe.cpp
    ../Host/CoreClrHost.cpp
    ../Host/AotHost.cpp
)

target_include_directories(gkNextDotNetProbe PRIVATE "${GK_SRC_ROOT}")

if(GK_DOTNET_BACKEND STREQUAL "AOT")
    if(NOT GK_DOTNET_AOT_LIB)
        message(FATAL_ERROR "GK_DOTNET_BACKEND=AOT requires GK_DOTNET_AOT_LIB")
    endif()
    target_compile_definitions(gkNextDotNetProbe PRIVATE GK_DOTNET_USE_AOT=1)
    target_link_libraries(gkNextDotNetProbe PRIVATE "${GK_DOTNET_AOT_LIB}")
else()
    if(NOT GK_DOTNET_HOSTPACK)
        message(FATAL_ERROR "GK_DOTNET_BACKEND=CoreCLR requires GK_DOTNET_HOSTPACK")
    endif()
    target_compile_definitions(gkNextDotNetProbe PRIVATE GK_DOTNET_USE_AOT=0)

    # Headers only. hostfxr itself is located and loaded at runtime, so nothing from the host pack
    # is linked in — see the note at the top of CoreClrHost.cpp.
    target_include_directories(gkNextDotNetProbe PRIVATE "${GK_DOTNET_HOSTPACK}")
    if(NOT WIN32)
        target_link_libraries(gkNextDotNetProbe PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()
