include_guard(GLOBAL)

function(gk_collect_vulkan_sdk_candidates outputVar)
    set(candidates "")

    if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        list(APPEND candidates "$ENV{VULKAN_SDK}")
    endif()

    set(projectSdkDir "${CMAKE_SOURCE_DIR}/external/VulkanSDK")
    if(EXISTS "${projectSdkDir}/.current_version")
        file(STRINGS "${projectSdkDir}/.current_version" currentVersion LIMIT_COUNT 1)
        if(currentVersion)
            list(APPEND candidates "${projectSdkDir}/${currentVersion}")
        endif()
    endif()
    file(GLOB projectCandidates LIST_DIRECTORIES true "${projectSdkDir}/*")
    list(APPEND candidates ${projectCandidates})

    if(WIN32)
        file(GLOB systemCandidates LIST_DIRECTORIES true "C:/VulkanSDK/*")
        list(APPEND candidates ${systemCandidates})
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        file(GLOB homeCandidates LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*")
        list(APPEND candidates ${homeCandidates})
    endif()

    list(REMOVE_DUPLICATES candidates)
    list(SORT candidates COMPARE NATURAL ORDER DESCENDING)
    set(${outputVar} "${candidates}" PARENT_SCOPE)
endfunction()

function(gk_resolve_desktop_vulkan_sdk outputVar)
    gk_collect_vulkan_sdk_candidates(candidates)
    foreach(candidate IN LISTS candidates)
        if(NOT IS_DIRECTORY "${candidate}")
            continue()
        endif()

        set(roots "${candidate}")
        if(APPLE)
            list(APPEND roots "${candidate}/macOS")
        elseif(NOT WIN32)
            list(APPEND roots "${candidate}/x86_64")
        endif()

        foreach(root IN LISTS roots)
            if(WIN32)
                if((EXISTS "${root}/Include/vulkan/vulkan.h" OR
                    EXISTS "${root}/include/vulkan/vulkan.h") AND
                   (EXISTS "${root}/Lib/vulkan-1.lib" OR
                    EXISTS "${root}/lib/vulkan-1.lib"))
                    set(${outputVar} "${root}" PARENT_SCOPE)
                    return()
                endif()
            elseif(APPLE)
                if(EXISTS "${root}/include/vulkan/vulkan.h" AND
                   EXISTS "${root}/lib/libvulkan.dylib")
                    set(${outputVar} "${root}" PARENT_SCOPE)
                    return()
                endif()
            elseif(EXISTS "${root}/include/vulkan/vulkan.h" AND
                   (EXISTS "${root}/lib/libvulkan.so" OR
                    EXISTS "${root}/lib/libvulkan.so.1" OR
                    EXISTS "${root}/lib/VulkanLoader/lib/libvulkan.so" OR
                    EXISTS "${root}/lib/VulkanLoader/lib/libvulkan.so.1"))
                set(${outputVar} "${root}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    set(${outputVar} "" PARENT_SCOPE)
endfunction()

function(gk_resolve_ios_vulkan_sdk outputVar)
    gk_collect_vulkan_sdk_candidates(candidates)
    foreach(candidate IN LISTS candidates)
        foreach(root IN ITEMS
            "${candidate}"
            "${candidate}/iOS"
            "${candidate}/../iOS"
        )
            if(EXISTS "${root}/include/vulkan/vulkan.h" AND
               EXISTS "${root}/lib/vulkan.framework/vulkan")
                get_filename_component(root "${root}" ABSOLUTE)
                set(${outputVar} "${root}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    set(${outputVar} "" PARENT_SCOPE)
endfunction()

function(gk_apply_desktop_vulkan_sdk sdkRoot)
    if(WIN32)
        set(includeDir "${sdkRoot}/Include")
        if(NOT EXISTS "${includeDir}/vulkan/vulkan.h")
            set(includeDir "${sdkRoot}/include")
        endif()
        set(libraryPath "${sdkRoot}/Lib/vulkan-1.lib")
        if(NOT EXISTS "${libraryPath}")
            set(libraryPath "${sdkRoot}/lib/vulkan-1.lib")
        endif()
    elseif(APPLE)
        set(includeDir "${sdkRoot}/include")
        set(libraryPath "${sdkRoot}/lib/libvulkan.dylib")
    else()
        set(includeDir "${sdkRoot}/include")
        foreach(candidateLibrary IN ITEMS
            "${sdkRoot}/lib/libvulkan.so"
            "${sdkRoot}/lib/libvulkan.so.1"
            "${sdkRoot}/lib/VulkanLoader/lib/libvulkan.so"
            "${sdkRoot}/lib/VulkanLoader/lib/libvulkan.so.1"
        )
            if(EXISTS "${candidateLibrary}")
                set(libraryPath "${candidateLibrary}")
                break()
            endif()
        endforeach()
    endif()

    set(ENV{VULKAN_SDK} "${sdkRoot}")
    set(Vulkan_INCLUDE_DIR "${includeDir}" CACHE PATH "Vulkan include directory" FORCE)
    set(Vulkan_LIBRARY "${libraryPath}" CACHE FILEPATH "Vulkan library" FORCE)
endfunction()

function(gk_apply_ios_vulkan_sdk sdkRoot)
    get_filename_component(sdkParent "${sdkRoot}" DIRECTORY)
    set(hostToolsRoot "${sdkRoot}")
    if(EXISTS "${sdkParent}/macOS/bin/slangc")
        set(hostToolsRoot "${sdkParent}/macOS")
    endif()

    set(ENV{VULKAN_SDK} "${hostToolsRoot}")
    set(Vulkan_FOUND TRUE PARENT_SCOPE)
    set(Vulkan_INCLUDE_DIRS "${sdkRoot}/include" PARENT_SCOPE)
    set(Vulkan_LIBRARIES "${sdkRoot}/lib/vulkan.framework/vulkan" PARENT_SCOPE)
    set(Vulkan_INCLUDE_DIR "${sdkRoot}/include" CACHE PATH "Vulkan include directory" FORCE)
    set(Vulkan_LIBRARY "${sdkRoot}/lib/vulkan.framework/vulkan" CACHE FILEPATH "Vulkan library" FORCE)
endfunction()

if(IOS)
    gk_resolve_ios_vulkan_sdk(vulkanSdkRoot)
    if(NOT vulkanSdkRoot)
        message(FATAL_ERROR "No usable iOS Vulkan SDK found. Run `gnb setup` or `gnb ios` first.")
    endif()
    message(STATUS "Using iOS Vulkan SDK: ${vulkanSdkRoot}")
    gk_apply_ios_vulkan_sdk("${vulkanSdkRoot}")

    set(MOLTENVK_LIBRARY "${MOLTENVK_ROOT}/lib/libMoltenVK.a" CACHE FILEPATH "MoltenVK static library" FORCE)
    if(NOT EXISTS "${MOLTENVK_LIBRARY}")
        message(FATAL_ERROR "MoltenVK library not found at ${MOLTENVK_LIBRARY}. Run `gnb setup` or `gnb ios` first.")
    endif()
elseif(ANDROID)
    find_package(Vulkan REQUIRED)
else()
    if(DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY AND NOT EXISTS "${Vulkan_LIBRARY}")
        unset(Vulkan_LIBRARY CACHE)
    endif()
    if(DEFINED Vulkan_INCLUDE_DIR AND Vulkan_INCLUDE_DIR AND
       NOT EXISTS "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.h")
        unset(Vulkan_INCLUDE_DIR CACHE)
    endif()

    gk_resolve_desktop_vulkan_sdk(vulkanSdkRoot)
    if(vulkanSdkRoot)
        message(STATUS "Using Vulkan SDK: ${vulkanSdkRoot}")
        gk_apply_desktop_vulkan_sdk("${vulkanSdkRoot}")
    endif()
    find_package(Vulkan REQUIRED)
endif()

find_package(VulkanMemoryAllocator CONFIG REQUIRED)

set(slangHintDirs "")
if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
    list(APPEND slangHintDirs "$ENV{VULKAN_SDK}")
endif()
gk_collect_vulkan_sdk_candidates(vulkanSdkCandidates)
list(APPEND slangHintDirs ${vulkanSdkCandidates})

if(DEFINED Vulkan_SLANGC AND Vulkan_SLANGC AND NOT EXISTS "${Vulkan_SLANGC}")
    unset(Vulkan_SLANGC CACHE)
endif()
find_program(Vulkan_SLANGC
    NAMES slangc
    HINTS ${slangHintDirs}
    PATH_SUFFIXES bin macOS/bin
)
if(NOT Vulkan_SLANGC)
    message(FATAL_ERROR "slangc not found. Run `gnb setup` and configure again.")
endif()
