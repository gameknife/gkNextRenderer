cmake_minimum_required(VERSION 3.10)

project(gkNextRenderer)

set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

foreach (OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/lib)
endforeach()

set (CMAKE_CXX_STANDARD 17)

if (WIN32)
	add_definitions(-DUNICODE -D_UNICODE)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif ()

if (MSVC)
	foreach (flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
		if (${flag_var} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
		endif()
	endforeach()
	
	add_compile_options("/MP")
endif()

if (UNIX AND !VCPKG_TARGET_ANDROID)
	
	add_definitions(-DUNIX)
	add_compile_options("-Wall")
	add_compile_options("-fvisibility=hidden")
	
	find_package(Backtrace REQUIRED)

	string(COMPARE EQUAL ${Backtrace_HEADER} "backtrace.h" BacktraceRequired)

	if (BacktraceRequired)
		add_compile_definitions(BOOST_STACKTRACE_USE_BACKTRACE)
	endif (BacktraceRequired)

endif ()

find_package(Boost REQUIRED COMPONENTS exception program_options) 

if (!VCPKG_TARGET_ANDROID)
find_package(glfw3 REQUIRED)
endif()

find_package(Vulkan REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(CURL REQUIRED)

if ( WITH_AVIF )
find_package(libavif CONFIG REQUIRED)
endif()

find_package(draco CONFIG REQUIRED)

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_path(CPP_BASE64_INCLUDE_DIRS "cpp-base64/base64.cpp")

IF (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
    message(STATUS ${Vulkan_LIBRARY})
ENDIF()

find_program(Vulkan_GLSLANG_VALIDATOR 
	NAMES glslangValidator 
	HINTS ENV VULKAN_SDK 
	PATH_SUFFIXES bin)
	
if (NOT Vulkan_GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found!")
endif()

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})
