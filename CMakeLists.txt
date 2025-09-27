cmake_minimum_required(VERSION 3.10)

project(gkNextRenderer)

set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# Set architecture based on platform
if (IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=iphoneos*] "arm64")
    endif()
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

# Set iOS bundle identifier if building for iOS
if (IOS)
    if( NOT CMAKE_IOS_BUNDLE_IDENTIFIER )
        set(CMAKE_IOS_BUNDLE_IDENTIFIER "com.gknext.renderer")
    endif()
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.gknext.renderer")
    set(MACOSX_BUNDLE_DISPLAY_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0")

    # Set app as iOS bundle
    set(MACOSX_BUNDLE TRUE)

    # iOS deployment target and architecture
    set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")  # 1=iPhone, 2=iPad

    # Set interface orientation (support all orientations)
    set(CMAKE_XCODE_ATTRIBUTE_UIInterfaceOrientation "UIInterfaceOrientationPortrait")
    set(CMAKE_XCODE_ATTRIBUTE_UIInterfaceOrientationPortrait "YES")
    set(CMAKE_XCODE_ATTRIBUTE_UIInterfaceOrientationLandscapeLeft "YES")
    set(CMAKE_XCODE_ATTRIBUTE_UIInterfaceOrientationLandscapeRight "YES")
    set(CMAKE_XCODE_ATTRIBUTE_UISupportedInterfaceOrientations "UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight")
    set(CMAKE_XCODE_ATTRIBUTE_UISupportedInterfaceOrientations~ipad "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight")

    # Link required iOS frameworks for audio and CoreAudio
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(FOUNDATION_FRAMEWORK Foundation)
	find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
	find_library(METAL_FRAMEWORK Metal)
	find_library(QUARTZCORE_FRAMEWORK QuartzCore)
	find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
	find_library(IOSURFACE_FRAMEWORK IOSurface)
	find_library(UIKIT_FRAMEWORK UIKit)

	# Metal.framework
	# Foundation.framework.
	# QuartzCore.framework
	# CoreGraphics.framework
	# IOSurface.framework
	# UIKit.framework

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Metal -framework AudioToolbox -framework AVFoundation -framework CoreAudio -framework CoreFoundation -framework Foundation -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework UIKit")
endif()

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

	# Configure PDB generation for minimal size (crash stack traces only)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Z7")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Z7")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

if (UNIX AND !ANDROID)
	add_definitions(-DUNIX)
	add_compile_options("-Wall")
	add_compile_options("-fvisibility=hidden")
endif ()

message(STATUS "SDL3_DIR: ${SDL3_DIR}")
find_package(SDL3 CONFIG REQUIRED)
# For iOS, we use MoltenVK instead of Vulkan
if (IOS)
    # Find MoltenVK for iOS
    find_library(MOLTENVK_LIBRARY MoltenVK)
    if (NOT MOLTENVK_LIBRARY)
        message(FATAL_ERROR "MoltenVK library not found for iOS!")
    endif()
    message(STATUS "Found MoltenVK: ${MOLTENVK_LIBRARY}")
else()
    find_package(Vulkan REQUIRED)
endif()
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(spdlog REQUIRED)

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