# VITURE distributes its desktop SDK outside vcpkg. Like Superluminal, it is
# enabled by the presence of a known local SDK rather than a user-facing switch.
set(GK_ENABLE_VITURE OFF)
set(GK_VITURE_ARCHIVE
    "${CMAKE_SOURCE_DIR}/external/viture/VITURE_XR_Glasses_SDK_for_MacOS_arm64.zip")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GK_VITURE_ARCHIVE}")

if(NOT APPLE OR IOS)
    message(STATUS "VITURE AR disabled: supported only on macOS arm64 desktop")
elseif(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(STATUS "VITURE AR disabled: macOS arm64 SDK is incompatible with ${CMAKE_SYSTEM_PROCESSOR}")
elseif(NOT EXISTS "${GK_VITURE_ARCHIVE}")
    message(STATUS "VITURE AR disabled: SDK archive not found at ${GK_VITURE_ARCHIVE}")
else()
    find_program(GK_VITURE_CODESIGN_EXECUTABLE codesign REQUIRED)
    set(GK_VITURE_SDK_ROOT "${CMAKE_BINARY_DIR}/external/viture-sdk")
    set(gkVitureSdkStamp "${GK_VITURE_SDK_ROOT}/archive.sha256")
    file(SHA256 "${GK_VITURE_ARCHIVE}" gkVitureArchiveHash)
    set(gkVitureExtractSdk OFF)

    if(NOT EXISTS "${gkVitureSdkStamp}" OR
       NOT EXISTS "${GK_VITURE_SDK_ROOT}/include/viture_glasses_provider.h" OR
       NOT EXISTS "${GK_VITURE_SDK_ROOT}/aarch64/libglasses.dylib" OR
       NOT EXISTS "${GK_VITURE_SDK_ROOT}/aarch64/libcarina_vio.dylib")
        set(gkVitureExtractSdk ON)
    else()
        file(READ "${gkVitureSdkStamp}" gkVitureExtractedHash)
        string(STRIP "${gkVitureExtractedHash}" gkVitureExtractedHash)
        if(NOT gkVitureExtractedHash STREQUAL gkVitureArchiveHash)
            set(gkVitureExtractSdk ON)
        endif()
    endif()

    if(gkVitureExtractSdk)
        file(REMOVE_RECURSE "${GK_VITURE_SDK_ROOT}")
        file(MAKE_DIRECTORY "${GK_VITURE_SDK_ROOT}")
        file(ARCHIVE_EXTRACT INPUT "${GK_VITURE_ARCHIVE}" DESTINATION "${GK_VITURE_SDK_ROOT}")
        file(WRITE "${gkVitureSdkStamp}" "${gkVitureArchiveHash}\n")
    endif()

    set(GK_VITURE_SDK_INCLUDE_DIR "${GK_VITURE_SDK_ROOT}/include")
    set(GK_VITURE_SDK_LIBRARY "${GK_VITURE_SDK_ROOT}/aarch64/libglasses.dylib")
    set(GK_VITURE_VIO_LIBRARY "${GK_VITURE_SDK_ROOT}/aarch64/libcarina_vio.dylib")
    set(GK_ENABLE_VITURE ON)
    message(STATUS "VITURE AR enabled from ${GK_VITURE_ARCHIVE}")
endif()
