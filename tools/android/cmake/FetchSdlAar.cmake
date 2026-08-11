cmake_minimum_required(VERSION 3.26)

foreach(requiredVariable IN ITEMS SDL_VERSION SDL_ARCHIVE_SHA256 SDL_AAR_SHA256 CACHE_DIR)
    if(NOT DEFINED ${requiredVariable} OR "${${requiredVariable}}" STREQUAL "")
        message(FATAL_ERROR "${requiredVariable} is required")
    endif()
endforeach()

set(aarName "SDL3-${SDL_VERSION}.aar")
set(aarPath "${CACHE_DIR}/${aarName}")
if(EXISTS "${aarPath}")
    file(SHA256 "${aarPath}" existingHash)
    if(existingHash STREQUAL SDL_AAR_SHA256)
        message(STATUS "Using cached ${aarName}")
        return()
    endif()
    file(REMOVE "${aarPath}")
endif()

file(MAKE_DIRECTORY "${CACHE_DIR}")
set(archivePath "${CACHE_DIR}/SDL3-devel-${SDL_VERSION}-android.zip")
set(downloadUrl "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-devel-${SDL_VERSION}-android.zip")
message(STATUS "Downloading ${downloadUrl}")
file(DOWNLOAD
    "${downloadUrl}"
    "${archivePath}"
    EXPECTED_HASH "SHA256=${SDL_ARCHIVE_SHA256}"
    SHOW_PROGRESS
    STATUS downloadStatus
)
list(GET downloadStatus 0 downloadCode)
if(NOT downloadCode EQUAL 0)
    list(GET downloadStatus 1 downloadMessage)
    file(REMOVE "${archivePath}")
    message(FATAL_ERROR "SDL3 download failed: ${downloadMessage}")
endif()

set(extractDir "${CACHE_DIR}/sdl-extract")
file(REMOVE_RECURSE "${extractDir}")
file(MAKE_DIRECTORY "${extractDir}")
file(ARCHIVE_EXTRACT INPUT "${archivePath}" DESTINATION "${extractDir}")
if(NOT EXISTS "${extractDir}/${aarName}")
    message(FATAL_ERROR "${aarName} was not found in the SDL3 Android archive")
endif()
file(SHA256 "${extractDir}/${aarName}" extractedHash)
if(NOT extractedHash STREQUAL SDL_AAR_SHA256)
    message(FATAL_ERROR "${aarName} checksum mismatch: ${extractedHash}")
endif()
file(RENAME "${extractDir}/${aarName}" "${aarPath}")
file(REMOVE_RECURSE "${extractDir}")
file(REMOVE "${archivePath}")
message(STATUS "Cached ${aarPath}")
