message(STATUS "SDL3_DIR: ${SDL3_DIR}")
find_package(SDL3 CONFIG REQUIRED)

if (APPLE AND NOT IOS)
    if (DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY AND NOT EXISTS "${Vulkan_LIBRARY}")
        message(STATUS "Clearing stale Vulkan library cache entry: ${Vulkan_LIBRARY}")
        unset(Vulkan_LIBRARY CACHE)
    endif()

    if (DEFINED Vulkan_INCLUDE_DIR AND Vulkan_INCLUDE_DIR AND
        NOT EXISTS "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.h")
        message(STATUS "Clearing stale Vulkan include cache entry: ${Vulkan_INCLUDE_DIR}")
        unset(Vulkan_INCLUDE_DIR CACHE)
    endif()

    if (NOT DEFINED ENV{VULKAN_SDK} OR "$ENV{VULKAN_SDK}" STREQUAL "")
        file(GLOB _vulkan_sdk_candidates LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*/macOS")
        list(SORT _vulkan_sdk_candidates COMPARE NATURAL ORDER DESCENDING)

        foreach(_vulkan_sdk_candidate IN LISTS _vulkan_sdk_candidates)
            if (EXISTS "${_vulkan_sdk_candidate}/include/vulkan/vulkan.h" AND
                EXISTS "${_vulkan_sdk_candidate}/lib/libvulkan.dylib")
                set(ENV{VULKAN_SDK} "${_vulkan_sdk_candidate}")
                message(STATUS "Using auto-detected Vulkan SDK: $ENV{VULKAN_SDK}")
                break()
            endif()
        endforeach()
    endif()
endif()

if (IOS)
    message(STATUS "MoltenVK: ${MOLTENVK_ROOT}")
    if (DEFINED MOLTENVK_ROOT)
        message(STATUS "Checking for MoltenVK at: ${MOLTENVK_ROOT}/lib/libMoltenVK.a")
        set(_moltenvk_candidate "${MOLTENVK_ROOT}/lib/libMoltenVK.a")
        if (EXISTS "${_moltenvk_candidate}")
            message(STATUS "Found candidate: ${_moltenvk_candidate}")
            set(MOLTENVK_LIBRARY "${_moltenvk_candidate}" CACHE FILEPATH "MoltenVK static library" FORCE)
        else()
             message(STATUS "Candidate not found")
        endif()
    endif()
    if (NOT MOLTENVK_LIBRARY)
        if (DEFINED MOLTENVK_ROOT)
            find_library(MOLTENVK_LIBRARY
                NAMES MoltenVK libMoltenVK
                PATHS "${MOLTENVK_ROOT}" "${MOLTENVK_ROOT}/lib"
                NO_DEFAULT_PATH)
        else()
            find_library(MOLTENVK_LIBRARY MoltenVK)
        endif()
    endif()
    if (NOT MOLTENVK_LIBRARY)
        message(FATAL_ERROR "MoltenVK library not found for iOS!")
    endif()
    message(STATUS "Found MoltenVK: ${MOLTENVK_LIBRARY}")
else()
    find_package(Vulkan REQUIRED)
endif()

if(NOT IOS AND NOT ANDROID)
    find_package(cpptrace CONFIG REQUIRED)
endif()

find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
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

set(_slang_hint_dirs)
list(APPEND _slang_hint_dirs ENV SLANG_ROOT ENV VULKAN_SDK)

if(DEFINED ENV{SLANG_ROOT})
    list(APPEND _slang_hint_dirs "$ENV{SLANG_ROOT}")
endif()

if(DEFINED SLANG_ROOT)
    list(APPEND _slang_hint_dirs "${SLANG_ROOT}")
endif()

set(_slang_candidate_roots)
list(APPEND _slang_candidate_roots
    "${CMAKE_SOURCE_DIR}/external/slang"
    "${CMAKE_SOURCE_DIR}/slang")

file(GLOB _slang_downloads "${CMAKE_SOURCE_DIR}/external/slang-*")
list(APPEND _slang_candidate_roots ${_slang_downloads})
list(REMOVE_DUPLICATES _slang_candidate_roots)

foreach(_slang_root ${_slang_candidate_roots})
    if(EXISTS "${_slang_root}")
        list(APPEND _slang_hint_dirs "${_slang_root}")
    endif()
endforeach()

find_program(Vulkan_SLANGC
	NAMES slangc
	HINTS ${_slang_hint_dirs}
	PATH_SUFFIXES bin)

if (NOT Vulkan_SLANGC)
    message(FATAL_ERROR "slangc not found!")
endif()
