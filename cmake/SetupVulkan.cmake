include_guard(GLOBAL)

function(gk_collect_vulkan_sdk_candidates outputVar)
    set(candidates "")

    if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        list(APPEND candidates "$ENV{VULKAN_SDK}")
        set(${outputVar} "${candidates}" PARENT_SCOPE)
        return()
    endif()

    set(projectSdkDir "${CMAKE_SOURCE_DIR}/external/VulkanSDK")
    if(EXISTS "${projectSdkDir}/.current_version")
        file(STRINGS "${projectSdkDir}/.current_version" currentVersion LIMIT_COUNT 1)
        if(currentVersion)
            list(APPEND candidates "${projectSdkDir}/${currentVersion}")
        endif()
    endif()
    file(GLOB projectCandidates LIST_DIRECTORIES true "${projectSdkDir}/*")
    list(SORT projectCandidates COMPARE NATURAL ORDER DESCENDING)
    list(APPEND candidates ${projectCandidates})

    if(WIN32)
        file(GLOB systemCandidates LIST_DIRECTORIES true "C:/VulkanSDK/*")
        list(SORT systemCandidates COMPARE NATURAL ORDER DESCENDING)
        list(APPEND candidates ${systemCandidates})
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        file(GLOB homeCandidates LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*")
        list(SORT homeCandidates COMPARE NATURAL ORDER DESCENDING)
        list(APPEND candidates ${homeCandidates})
    endif()

    list(REMOVE_DUPLICATES candidates)
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
            "${candidate}/macOS"
        )
            if(EXISTS "${root}/include/vulkan/vulkan.h" AND
               EXISTS "${root}/bin/slangc" AND
               EXISTS "${root}/lib/MoltenVK.xcframework/Info.plist")
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
    if(NOT CMAKE_OSX_SYSROOT STREQUAL "iphoneos")
        message(FATAL_ERROR
            "Unsupported iOS sysroot '${CMAKE_OSX_SYSROOT}'. Only iphoneos device builds are supported.")
    endif()

    set(moltenVKSlice "ios-arm64")
    set(moltenVKLibrary
        "${sdkRoot}/lib/MoltenVK.xcframework/${moltenVKSlice}/libMoltenVK.a")
    if(NOT EXISTS "${moltenVKLibrary}")
        message(FATAL_ERROR
            "MoltenVK slice not found. SDK root: '${sdkRoot}', sysroot: "
            "'${CMAKE_OSX_SYSROOT}', expected: '${moltenVKLibrary}'")
    endif()

    set(ENV{VULKAN_SDK} "${sdkRoot}")
    set(Vulkan_FOUND TRUE PARENT_SCOPE)
    set(Vulkan_INCLUDE_DIRS "${sdkRoot}/include" PARENT_SCOPE)
    set(Vulkan_LIBRARIES "${moltenVKLibrary}" PARENT_SCOPE)
    set(Vulkan_INCLUDE_DIR "${sdkRoot}/include" CACHE PATH "Vulkan include directory" FORCE)
    set(Vulkan_LIBRARY "${moltenVKLibrary}" CACHE FILEPATH "Vulkan library" FORCE)
    set(MOLTENVK_LIBRARY "${moltenVKLibrary}" CACHE FILEPATH "MoltenVK static library" FORCE)
endfunction()

if(IOS)
    gk_resolve_ios_vulkan_sdk(vulkanSdkRoot)
    if(NOT vulkanSdkRoot)
        if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
            message(FATAL_ERROR
                "VULKAN_SDK is set to unusable Apple SDK path '$ENV{VULKAN_SDK}'. Expected "
                "include/vulkan/vulkan.h, bin/slangc and lib/MoltenVK.xcframework/Info.plist.")
        else()
            message(FATAL_ERROR
                "No usable Apple Vulkan SDK found for iOS. Expected include/vulkan/vulkan.h, "
                "bin/slangc and lib/MoltenVK.xcframework/Info.plist. Run `gnb setup` first.")
        endif()
    endif()
    message(STATUS "Using Apple Vulkan SDK for iOS: ${vulkanSdkRoot}")
    gk_apply_ios_vulkan_sdk("${vulkanSdkRoot}")
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

# tools/slang is populated by the Linux ARM64 setup path. It contains a
# host-specific executable, so never let it shadow the native slangc bundled
# with the macOS Vulkan SDK in a workspace shared by both hosts.
set(localSlangHintDirs "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND localSlangHintDirs "${CMAKE_SOURCE_DIR}/tools/slang")
endif()

if(DEFINED Vulkan_SLANGC AND Vulkan_SLANGC AND NOT EXISTS "${Vulkan_SLANGC}")
    unset(Vulkan_SLANGC CACHE)
endif()
if(APPLE AND DEFINED Vulkan_SLANGC AND Vulkan_SLANGC MATCHES "/tools/slang/")
    unset(Vulkan_SLANGC CACHE)
endif()
find_program(Vulkan_SLANGC
    NAMES slangc
    HINTS
        ${localSlangHintDirs}
        ${slangHintDirs}
    PATH_SUFFIXES bin macOS/bin
)
if(NOT Vulkan_SLANGC)
    message(FATAL_ERROR "slangc not found. Run `gnb setup` and configure again.")
endif()
