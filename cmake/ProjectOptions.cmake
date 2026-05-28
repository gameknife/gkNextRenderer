# --- Standard Project Options ---
add_library(gk_project_options INTERFACE)

if (WIN32)
    target_compile_definitions(gk_project_options INTERFACE
        UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif ()

if (MSVC)
	# 使用 CMake 3.15+ 的标准方式设置 MSVC 运行时库为静态链接
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

	target_compile_options(gk_project_options INTERFACE "/MP")
    target_compile_options(gk_project_options INTERFACE "$<$<C_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

endif()

if (UNIX AND !ANDROID)
	target_compile_definitions(gk_project_options INTERFACE UNIX)
	target_compile_options(gk_project_options INTERFACE "-Wall")
	target_compile_options(gk_project_options INTERFACE "-fvisibility=hidden")
endif ()

if (APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,-no_warn_duplicate_libraries" GK_HAS_NO_WARN_DUPLICATE_LIBRARIES)
    if (GK_HAS_NO_WARN_DUPLICATE_LIBRARIES)
        target_link_options(gk_project_options INTERFACE "-Wl,-no_warn_duplicate_libraries")
    endif()
endif()

# LTO/LTCG - Disabled by default (slows down build), enable with -DENABLE_LTO=ON
option(ENABLE_LTO "Enable Link Time Optimization (LTO/LTCG)" OFF)
if (ENABLE_LTO)
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if (ipo_supported)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
            message(STATUS "LTO/LTCG enabled for ${CMAKE_BUILD_TYPE} build")
        else()
            message(WARNING "LTO/LTCG not supported: ${ipo_error}")
        endif()
    endif()
endif()
