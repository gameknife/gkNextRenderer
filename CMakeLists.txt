cmake_minimum_required(VERSION 3.10)

project(gkNextRenderer)

set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_OSX_ARCHITECTURES "arm64")

foreach (OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/lib)
endforeach()

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_C_STANDARD 11)

if (WIN32)
	add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)
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

if (UNIX AND !ANDROID)
	
	add_definitions(-DUNIX)
	add_compile_options("-Wall")
	add_compile_options("-fvisibility=hidden")
	
	#find_package(Backtrace REQUIRED)

	#string(COMPARE EQUAL ${Backtrace_HEADER} "backtrace.h" BacktraceRequired)

	#if (BacktraceRequired)
	#	add_compile_definitions(BOOST_STACKTRACE_USE_BACKTRACE)
	#endif (BacktraceRequired)
endif ()

#find_package(Boost REQUIRED COMPONENTS exception program_options)

if (ANDROID)
find_package(game-activity REQUIRED CONFIG)
else()
find_package(cpptrace CONFIG REQUIRED)
find_package(glfw3 REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
endif()

#find_package(cxxopts CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

find_package(fmt CONFIG REQUIRED)


if ( WITH_AVIF )
find_package(libavif CONFIG REQUIRED)
endif()

if( WITH_SUPERLUMINAL )
set(SuperluminalAPI_USE_STATIC_RUNTIME 1)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "C:/Program Files/Superluminal/Performance/API")
find_package(SuperluminalAPI REQUIRED)
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

find_program(Vulkan_SLANGC 
	NAMES slangc 
	HINTS ENV VULKAN_SDK 
	PATH_SUFFIXES bin)
	
if (NOT Vulkan_GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found!")
endif()

if (NOT Vulkan_SLANGC)
    message(FATAL_ERROR "slangc not found!")
endif()

set(MAIN_PROJECT "gkNextRenderer")
add_subdirectory(assets)
add_subdirectory(src)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ${MAIN_PROJECT})