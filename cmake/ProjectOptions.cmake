if (MSVC)
	# 使用 CMake 3.15+ 的标准方式设置 MSVC 运行时库为静态链接
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

option(GK_ENABLE_ASAN "Enable AddressSanitizer instrumentation for native code" OFF)
if (GK_ENABLE_ASAN)
    if (ANDROID OR IOS)
        message(FATAL_ERROR "GK_ENABLE_ASAN is not supported by the Android or iOS build configurations")
    elseif(MSVC)
        # MSVC selects the matching ASan runtime library automatically. /INCREMENTAL
        # is incompatible with ASan, so gk_enable_fast_dev_link() also stays disabled.
        # The vcpkg x64-windows-static libraries are intentionally not ASan-instrumented.
        # Keep the STL annotation mode consistent with those libraries; ASan's normal
        # heap/stack/global instrumentation remains enabled.
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
        add_compile_definitions(_DISABLE_STL_ANNOTATION)

        get_filename_component(GK_ASAN_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(GK_ASAN_RUNTIME_DLL "${GK_ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.dll")
        if(NOT EXISTS "${GK_ASAN_RUNTIME_DLL}")
            message(FATAL_ERROR
                "MSVC AddressSanitizer runtime is missing: ${GK_ASAN_RUNTIME_DLL}. "
                "Install the AddressSanitizer component in Visual Studio Installer.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    else()
        message(FATAL_ERROR
            "GK_ENABLE_ASAN requires MSVC, Clang, or GCC; got '${CMAKE_CXX_COMPILER_ID}'")
    endif()

    message(STATUS "AddressSanitizer enabled")
endif()

if (APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,-no_warn_duplicate_libraries" GK_HAS_NO_WARN_DUPLICATE_LIBRARIES)
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
