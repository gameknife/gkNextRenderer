message(STATUS "SDL3_DIR: ${SDL3_DIR}")
find_package(SDL3 CONFIG REQUIRED)

set(GKNEXT_DISABLE_VULKAN_AUTO_FETCH OFF CACHE BOOL "Disable automatic project-managed Vulkan SDK download during CMake configure")

function(_gk_try_use_desktop_vulkan_sdk sdkCandidate outVar)
    set(_resolved "")

    if (WIN32)
        if ((EXISTS "${sdkCandidate}/Include/vulkan/vulkan.h" OR EXISTS "${sdkCandidate}/include/vulkan/vulkan.h") AND
            (EXISTS "${sdkCandidate}/Lib/vulkan-1.lib" OR EXISTS "${sdkCandidate}/lib/vulkan-1.lib"))
            set(_resolved "${sdkCandidate}")
        endif()
    elseif (APPLE)
        set(_resolved "")

        if (EXISTS "${sdkCandidate}/include/vulkan/vulkan.h" AND
            EXISTS "${sdkCandidate}/lib/libvulkan.dylib")
            set(_resolved "${sdkCandidate}")
        elseif (EXISTS "${sdkCandidate}/macOS/include/vulkan/vulkan.h" AND
                EXISTS "${sdkCandidate}/macOS/lib/libvulkan.dylib")
            set(_resolved "${sdkCandidate}/macOS")
        endif()
    else()
        if (EXISTS "${sdkCandidate}/include/vulkan/vulkan.h" AND
            (EXISTS "${sdkCandidate}/lib/libvulkan.so" OR
             EXISTS "${sdkCandidate}/lib/libvulkan.so.1" OR
             EXISTS "${sdkCandidate}/lib/VulkanLoader/lib/libvulkan.so" OR
             EXISTS "${sdkCandidate}/lib/VulkanLoader/lib/libvulkan.so.1"))
            set(_resolved "${sdkCandidate}")
        elseif (EXISTS "${sdkCandidate}/x86_64/include/vulkan/vulkan.h" AND
                (EXISTS "${sdkCandidate}/x86_64/lib/libvulkan.so" OR
                 EXISTS "${sdkCandidate}/x86_64/lib/libvulkan.so.1" OR
                 EXISTS "${sdkCandidate}/x86_64/lib/VulkanLoader/lib/libvulkan.so" OR
                 EXISTS "${sdkCandidate}/x86_64/lib/VulkanLoader/lib/libvulkan.so.1"))
            set(_resolved "${sdkCandidate}/x86_64")
        endif()
    endif()

    if (_resolved)
        set(${outVar} "${_resolved}" PARENT_SCOPE)
    else()
        set(${outVar} "" PARENT_SCOPE)
    endif()
endfunction()

function(_gk_try_use_ios_vulkan_sdk sdkCandidate outVar)
    set(_resolved "")

    if (EXISTS "${sdkCandidate}/include/vulkan/vulkan.h" AND
        EXISTS "${sdkCandidate}/lib/vulkan.framework/vulkan")
        set(_resolved "${sdkCandidate}")
    elseif (EXISTS "${sdkCandidate}/iOS/include/vulkan/vulkan.h" AND
            EXISTS "${sdkCandidate}/iOS/lib/vulkan.framework/vulkan")
        set(_resolved "${sdkCandidate}/iOS")
    elseif (EXISTS "${sdkCandidate}/../iOS/include/vulkan/vulkan.h" AND
            EXISTS "${sdkCandidate}/../iOS/lib/vulkan.framework/vulkan")
        set(_resolved "${sdkCandidate}/../iOS")
    endif()

    if (_resolved)
        set(${outVar} "${_resolved}" PARENT_SCOPE)
    else()
        set(${outVar} "" PARENT_SCOPE)
    endif()
endfunction()

function(_gk_apply_vulkan_sdk sdkRoot)
    if (WIN32)
        set(_include_dir "${sdkRoot}/Include")
        if (NOT EXISTS "${_include_dir}/vulkan/vulkan.h")
            set(_include_dir "${sdkRoot}/include")
        endif()
        set(_library_path "${sdkRoot}/Lib/vulkan-1.lib")
        if (NOT EXISTS "${_library_path}")
            set(_library_path "${sdkRoot}/lib/vulkan-1.lib")
        endif()
    elseif (APPLE)
        set(_include_dir "${sdkRoot}/include")
        set(_library_path "${sdkRoot}/lib/libvulkan.dylib")
    else()
        set(_include_dir "${sdkRoot}/include")
        if (EXISTS "${sdkRoot}/lib/libvulkan.so")
            set(_library_path "${sdkRoot}/lib/libvulkan.so")
        elseif (EXISTS "${sdkRoot}/lib/libvulkan.so.1")
            set(_library_path "${sdkRoot}/lib/libvulkan.so.1")
        elseif (EXISTS "${sdkRoot}/lib/VulkanLoader/lib/libvulkan.so")
            set(_library_path "${sdkRoot}/lib/VulkanLoader/lib/libvulkan.so")
        else()
            set(_library_path "${sdkRoot}/lib/VulkanLoader/lib/libvulkan.so.1")
        endif()
    endif()

    set(ENV{VULKAN_SDK} "${sdkRoot}")
    set(Vulkan_INCLUDE_DIR "${_include_dir}" CACHE PATH "Vulkan include directory" FORCE)
    set(Vulkan_LIBRARY "${_library_path}" CACHE FILEPATH "Vulkan library" FORCE)
endfunction()

function(_gk_apply_ios_vulkan_sdk sdkRoot)
    set(_include_dir "${sdkRoot}/include")
    set(_library_path "${sdkRoot}/lib/vulkan.framework/vulkan")

    get_filename_component(_sdk_parent "${sdkRoot}" DIRECTORY)
    set(_host_tools_root "${sdkRoot}")
    if (EXISTS "${_sdk_parent}/macOS/bin/slangc")
        set(_host_tools_root "${_sdk_parent}/macOS")
    endif()

    set(ENV{VULKAN_SDK} "${_host_tools_root}")
    set(Vulkan_FOUND TRUE)
    set(Vulkan_INCLUDE_DIR "${_include_dir}" CACHE PATH "Vulkan include directory" FORCE)
    set(Vulkan_LIBRARY "${_library_path}" CACHE FILEPATH "Vulkan library" FORCE)
    set(Vulkan_INCLUDE_DIRS "${_include_dir}" PARENT_SCOPE)
    set(Vulkan_LIBRARIES "${_library_path}" PARENT_SCOPE)
endfunction()

function(_gk_collect_vulkan_sdk_candidates outVar)
    set(_vulkan_sdk_candidates)

    if (DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        list(APPEND _vulkan_sdk_candidates "$ENV{VULKAN_SDK}")
    endif()

    set(_project_vulkan_sdk_dir "${CMAKE_SOURCE_DIR}/external/VulkanSDK")
    if (EXISTS "${_project_vulkan_sdk_dir}/.current_version")
        file(READ "${_project_vulkan_sdk_dir}/.current_version" _current_vulkan_sdk_version)
        string(STRIP "${_current_vulkan_sdk_version}" _current_vulkan_sdk_version)
        if (_current_vulkan_sdk_version)
            list(APPEND _vulkan_sdk_candidates "${_project_vulkan_sdk_dir}/${_current_vulkan_sdk_version}")
        endif()
    endif()
    file(GLOB _project_vulkan_sdk_candidates LIST_DIRECTORIES true "${_project_vulkan_sdk_dir}/*")
    list(APPEND _vulkan_sdk_candidates ${_project_vulkan_sdk_candidates})

    if (WIN32)
        file(GLOB _system_vulkan_sdk_candidates LIST_DIRECTORIES true "C:/VulkanSDK/*")
        list(APPEND _vulkan_sdk_candidates ${_system_vulkan_sdk_candidates})
    elseif (APPLE)
        if (DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            file(GLOB _home_vulkan_sdk_candidates LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*")
            list(APPEND _vulkan_sdk_candidates ${_home_vulkan_sdk_candidates})
        endif()
        if (DEFINED ENV{USER} AND NOT "$ENV{USER}" STREQUAL "")
            file(GLOB _user_vulkan_sdk_candidates LIST_DIRECTORIES true "/Users/$ENV{USER}/VulkanSDK/*")
            list(APPEND _vulkan_sdk_candidates ${_user_vulkan_sdk_candidates})
        endif()
    else()
        if (DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            file(GLOB _home_vulkan_sdk_candidates LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*")
            list(APPEND _vulkan_sdk_candidates ${_home_vulkan_sdk_candidates})
        endif()
    endif()

    list(REMOVE_DUPLICATES _vulkan_sdk_candidates)
    list(SORT _vulkan_sdk_candidates COMPARE NATURAL ORDER DESCENDING)
    set(${outVar} "${_vulkan_sdk_candidates}" PARENT_SCOPE)
endfunction()

function(_gk_resolve_desktop_vulkan_sdk outVar)
    _gk_collect_vulkan_sdk_candidates(_vulkan_sdk_candidates)
    set(_vulkan_sdk_root "")

    foreach(_vulkan_sdk_candidate IN LISTS _vulkan_sdk_candidates)
        _gk_try_use_desktop_vulkan_sdk("${_vulkan_sdk_candidate}" _vulkan_sdk_root)
        if (_vulkan_sdk_root)
            break()
        endif()
    endforeach()

    set(${outVar} "${_vulkan_sdk_root}" PARENT_SCOPE)
endfunction()

function(_gk_resolve_ios_vulkan_sdk outVar)
    _gk_collect_vulkan_sdk_candidates(_vulkan_sdk_candidates)
    set(_vulkan_sdk_root "")

    foreach(_vulkan_sdk_candidate IN LISTS _vulkan_sdk_candidates)
        _gk_try_use_ios_vulkan_sdk("${_vulkan_sdk_candidate}" _vulkan_sdk_root)
        if (_vulkan_sdk_root)
            break()
        endif()
    endforeach()

    set(${outVar} "${_vulkan_sdk_root}" PARENT_SCOPE)
endfunction()

function(_gk_fetch_project_vulkan_sdk outVar)
    if (WIN32)
        execute_process(
            COMMAND cmd /c "${CMAKE_SOURCE_DIR}/gnb.bat" deps fetch vulkan
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _fetch_result)
    else()
        execute_process(
            COMMAND sh "${CMAKE_SOURCE_DIR}/gnb.sh" deps fetch vulkan
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _fetch_result)
    endif()

    set(${outVar} "${_fetch_result}" PARENT_SCOPE)
endfunction()

if (IOS)
    _gk_resolve_ios_vulkan_sdk(_gk_ios_vulkan_sdk_root)

    if (NOT _gk_ios_vulkan_sdk_root AND NOT GKNEXT_DISABLE_VULKAN_AUTO_FETCH)
        message(STATUS "No usable iOS Vulkan SDK found. Fetching project-managed SDK via gnb.")
        _gk_fetch_project_vulkan_sdk(_gk_vulkan_fetch_result)
        if (NOT _gk_vulkan_fetch_result EQUAL 0)
            message(FATAL_ERROR "Automatic Vulkan SDK download failed (gnb exit code ${_gk_vulkan_fetch_result}).")
        endif()
        _gk_resolve_ios_vulkan_sdk(_gk_ios_vulkan_sdk_root)
    endif()

    if (_gk_ios_vulkan_sdk_root)
        message(STATUS "Using iOS Vulkan SDK: ${_gk_ios_vulkan_sdk_root}")
        _gk_apply_ios_vulkan_sdk("${_gk_ios_vulkan_sdk_root}")
    endif()
elseif (NOT ANDROID)
    if (DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY AND NOT EXISTS "${Vulkan_LIBRARY}")
        message(STATUS "Clearing stale Vulkan library cache entry: ${Vulkan_LIBRARY}")
        unset(Vulkan_LIBRARY CACHE)
    endif()

    if (DEFINED Vulkan_INCLUDE_DIR AND Vulkan_INCLUDE_DIR AND
        NOT EXISTS "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.h")
        message(STATUS "Clearing stale Vulkan include cache entry: ${Vulkan_INCLUDE_DIR}")
        unset(Vulkan_INCLUDE_DIR CACHE)
    endif()

    _gk_resolve_desktop_vulkan_sdk(_vulkan_sdk_root)

    if (NOT _vulkan_sdk_root AND NOT GKNEXT_DISABLE_VULKAN_AUTO_FETCH)
        message(STATUS "No usable Vulkan SDK found. Fetching project-managed SDK via gnb.")
        _gk_fetch_project_vulkan_sdk(_gk_vulkan_fetch_result)
        if (NOT _gk_vulkan_fetch_result EQUAL 0)
            message(FATAL_ERROR "Automatic Vulkan SDK download failed (gnb exit code ${_gk_vulkan_fetch_result}).")
        endif()
        _gk_resolve_desktop_vulkan_sdk(_vulkan_sdk_root)
    endif()

    if (_vulkan_sdk_root)
        message(STATUS "Using Vulkan SDK: ${_vulkan_sdk_root}")
        _gk_apply_vulkan_sdk("${_vulkan_sdk_root}")
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

find_package(VulkanMemoryAllocator CONFIG REQUIRED)

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

IF (NOT IOS AND NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
    if (DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY)
        message(STATUS ${Vulkan_LIBRARY})
    endif()
ENDIF()

set(_slang_hint_dirs)
set(_project_managed_slangc "")

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
        if (NOT _project_managed_slangc AND EXISTS "${_slang_root}/bin/slangc")
            set(_project_managed_slangc "${_slang_root}/bin/slangc")
        endif()
    endif()
endforeach()

if(DEFINED ENV{SLANG_ROOT})
    list(APPEND _slang_hint_dirs "$ENV{SLANG_ROOT}")
endif()

if(DEFINED SLANG_ROOT)
    list(APPEND _slang_hint_dirs "${SLANG_ROOT}")
endif()

if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
    list(APPEND _slang_hint_dirs "$ENV{VULKAN_SDK}")
endif()

list(REMOVE_DUPLICATES _slang_hint_dirs)

if (DEFINED Vulkan_SLANGC AND Vulkan_SLANGC AND NOT EXISTS "${Vulkan_SLANGC}")
    message(STATUS "Clearing stale slangc cache entry: ${Vulkan_SLANGC}")
    unset(Vulkan_SLANGC CACHE)
endif()

if (_project_managed_slangc)
    set(Vulkan_SLANGC "${_project_managed_slangc}" CACHE FILEPATH "Preferred project-managed slangc executable" FORCE)
endif()

find_program(Vulkan_SLANGC
	NAMES slangc
	HINTS ${_slang_hint_dirs}
	PATH_SUFFIXES bin)

if (NOT Vulkan_SLANGC)
    message(FATAL_ERROR "slangc not found!")
endif()
