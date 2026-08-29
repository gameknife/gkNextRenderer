include_guard(GLOBAL)

# ============================================================================
# MobileApplications.cmake - the mobile application registry
# ============================================================================
# Android and iOS package exactly one application per build. Which one is a
# choice made outside CMake (Gradle names a target, `gnb ios build --app` names
# a target), so the engine only has to answer two questions about it: where
# its directory is and what identity it ships under.
#
# Both answers come from src/Application/MobileApplications.json, which
# tools/android and gnb read as well. Nothing here knows about gkNextRenderer.
#
# This file is included by both the engine project and the standalone Android
# driver project in tools/android, so it must not depend on any variable those
# two do not share.
# ============================================================================

get_filename_component(GK_MOBILE_APPLICATIONS_MANIFEST
    "${CMAKE_CURRENT_LIST_DIR}/../Application/MobileApplications.json" ABSOLUTE)
if(NOT EXISTS "${GK_MOBILE_APPLICATIONS_MANIFEST}")
    message(FATAL_ERROR "Mobile application manifest not found: ${GK_MOBILE_APPLICATIONS_MANIFEST}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GK_MOBILE_APPLICATIONS_MANIFEST}")

file(READ "${GK_MOBILE_APPLICATIONS_MANIFEST}" gkMobileManifestJson)

set(GK_MOBILE_APPLICATIONS "")
set(GK_ANDROID_APPLICATIONS "")
set(GK_IOS_APPLICATIONS "")

string(JSON gkMobileApplicationCount LENGTH "${gkMobileManifestJson}" applications)
math(EXPR gkMobileLastIndex "${gkMobileApplicationCount} - 1")
foreach(gkMobileIndex RANGE 0 ${gkMobileLastIndex})
    string(JSON gkMobileEntry GET "${gkMobileManifestJson}" applications ${gkMobileIndex})

    foreach(gkMobileField target directory label androidId iosBundleId)
        string(JSON gkMobileValue ERROR_VARIABLE gkMobileFieldError
            GET "${gkMobileEntry}" ${gkMobileField})
        if(gkMobileFieldError)
            message(FATAL_ERROR
                "MobileApplications.json entry ${gkMobileIndex} is missing '${gkMobileField}'")
        endif()
        set(gkMobile_${gkMobileField} "${gkMobileValue}")
    endforeach()

    set(gkMobileTarget "${gkMobile_target}")
    list(APPEND GK_MOBILE_APPLICATIONS "${gkMobileTarget}")
    set(GK_MOBILE_APP_${gkMobileTarget}_DIRECTORY "${gkMobile_directory}")
    set(GK_MOBILE_APP_${gkMobileTarget}_LABEL "${gkMobile_label}")
    set(GK_MOBILE_APP_${gkMobileTarget}_ANDROID_ID "${gkMobile_androidId}")
    set(GK_MOBILE_APP_${gkMobileTarget}_IOS_BUNDLE_ID "${gkMobile_iosBundleId}")

    string(JSON gkMobileDotNet ERROR_VARIABLE gkMobileDotNetError GET "${gkMobileEntry}" requiresDotNet)
    if(gkMobileDotNetError)
        set(gkMobileDotNet OFF)
    endif()
    set(GK_MOBILE_APP_${gkMobileTarget}_REQUIRES_DOTNET "${gkMobileDotNet}")

    string(JSON gkMobilePlatformCount LENGTH "${gkMobileEntry}" platforms)
    math(EXPR gkMobileLastPlatform "${gkMobilePlatformCount} - 1")
    set(gkMobilePlatforms "")
    foreach(gkMobilePlatformIndex RANGE 0 ${gkMobileLastPlatform})
        string(JSON gkMobilePlatform GET "${gkMobileEntry}" platforms ${gkMobilePlatformIndex})
        list(APPEND gkMobilePlatforms "${gkMobilePlatform}")
    endforeach()
    set(GK_MOBILE_APP_${gkMobileTarget}_PLATFORMS "${gkMobilePlatforms}")
    if("android" IN_LIST gkMobilePlatforms)
        list(APPEND GK_ANDROID_APPLICATIONS "${gkMobileTarget}")
    endif()
    if("ios" IN_LIST gkMobilePlatforms)
        list(APPEND GK_IOS_APPLICATIONS "${gkMobileTarget}")
    endif()
endforeach()

# Resolves a possibly-empty, possibly-differently-cased application name against the registry for
# one platform ("android" or "ios"). An empty name selects the first application the manifest lists
# for that platform, which is what makes gkNextRenderer the default without naming it here.
function(gk_resolve_mobile_application platform requestedName outputVariable)
    if(platform STREQUAL "android")
        set(candidates ${GK_ANDROID_APPLICATIONS})
    elseif(platform STREQUAL "ios")
        set(candidates ${GK_IOS_APPLICATIONS})
    else()
        message(FATAL_ERROR "Unknown mobile platform '${platform}'")
    endif()

    if(NOT requestedName)
        list(GET candidates 0 resolved)
        set(${outputVariable} "${resolved}" PARENT_SCOPE)
        return()
    endif()

    foreach(candidate IN LISTS candidates)
        string(TOLOWER "${candidate}" candidateLower)
        string(TOLOWER "${requestedName}" requestedLower)
        if(candidateLower STREQUAL requestedLower)
            set(${outputVariable} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    string(REPLACE ";" ", " availableText "${candidates}")
    message(FATAL_ERROR
        "'${requestedName}' is not a ${platform} application.\n"
        "Available: ${availableText}\n"
        "Add it to ${GK_MOBILE_APPLICATIONS_MANIFEST} to package it for ${platform}.")
endfunction()
