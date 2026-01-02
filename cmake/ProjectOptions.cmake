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
	foreach (flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
		if (${flag_var} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
		endif()
	endforeach()

	target_compile_options(gk_project_options INTERFACE "/MP")
    target_compile_options(gk_project_options INTERFACE "$<$<C_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

	# Configure PDB generation for minimal size (crash stack traces only)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Z7")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Z7")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

if (UNIX AND !ANDROID)
	target_compile_definitions(gk_project_options INTERFACE UNIX)
	target_compile_options(gk_project_options INTERFACE "-Wall")
	target_compile_options(gk_project_options INTERFACE "-fvisibility=hidden")
endif ()
