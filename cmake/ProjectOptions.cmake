# --- Standard Project Options ---
add_library(gk_project_options INTERFACE)

if (WIN32)
    target_compile_definitions(gk_project_options INTERFACE
        UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
endif ()

if (MINGW)
    target_link_options(gk_project_options INTERFACE "-municode")
    target_compile_definitions(gk_project_options INTERFACE MINGW=1)
endif()

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

# LTCG
# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
