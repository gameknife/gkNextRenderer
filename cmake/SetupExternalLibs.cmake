# External downloads are prepared by `gnb setup`. CMake only validates paths.

function(gk_require_path output_var glob_pattern sentinel message_text)
    file(GLOB _candidate_dirs "${glob_pattern}")
    list(SORT _candidate_dirs)
    foreach(_candidate_dir IN LISTS _candidate_dirs)
        if(EXISTS "${_candidate_dir}/${sentinel}")
            set(${output_var} "${_candidate_dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "${message_text}")
endfunction()

# --- NVIDIA Streamline ---
if(WITH_STREAMLINE)
    gk_require_path(
        STREAMLINE_ROOT
        "${CMAKE_SOURCE_DIR}/external/streamline-*"
        "include/sl.h"
        "Streamline SDK missing under external/. Run `gnb setup` first."
    )
    set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_ROOT}/include")
    set(STREAMLINE_LIB_DIR "${STREAMLINE_ROOT}/lib/x64")
    set(STREAMLINE_BIN_DIR "${STREAMLINE_ROOT}/bin/x64")
    message(STATUS "Streamline Root: ${STREAMLINE_ROOT}")
endif()

# --- TypeScript Compiler (for QuickJS) ---
if(WITH_QUICKJS)
    if(CMAKE_HOST_WIN32)
        set(TSC_FILENAME "tsc.exe")
    else()
        set(TSC_FILENAME "tsc")
    endif()

    set(TSC_EXECUTABLE "${CMAKE_SOURCE_DIR}/tools/tsc/${TSC_FILENAME}")
    if(NOT EXISTS "${TSC_EXECUTABLE}")
        message(FATAL_ERROR "TypeScript compiler missing at ${TSC_EXECUTABLE}. Run `gnb setup` first.")
    endif()
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${TSC_EXECUTABLE}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    message(STATUS "TSC Executable: ${TSC_EXECUTABLE}")
endif()

# --- MoltenVK (iOS only) ---
if(IOS)
    gk_require_path(
        MOLTENVK_SOURCE_ROOT
        "${CMAKE_SOURCE_DIR}/external/moltenvk-*"
        "MoltenVK"
        "MoltenVK missing under external/. Run `gnb setup` or `gnb ios` first."
    )

    unset(MVK_SOURCE_PATH)
    foreach(_mvk_candidate IN ITEMS
        "${MOLTENVK_SOURCE_ROOT}/MoltenVK/static/MoltenVK.xcframework/ios-arm64"
        "${MOLTENVK_SOURCE_ROOT}/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64"
    )
        if(EXISTS "${_mvk_candidate}/libMoltenVK.a")
            set(MVK_SOURCE_PATH "${_mvk_candidate}")
            break()
        endif()
    endforeach()

    if(NOT MVK_SOURCE_PATH)
        message(FATAL_ERROR "MoltenVK missing under external/. Run `gnb setup` or `gnb ios` first.")
    endif()

    file(MAKE_DIRECTORY "${MOLTENVK_SOURCE_ROOT}/lib")
    configure_file("${MVK_SOURCE_PATH}/libMoltenVK.a" "${MOLTENVK_SOURCE_ROOT}/lib/libMoltenVK.a" COPYONLY)
    set(MOLTENVK_ROOT "${MOLTENVK_SOURCE_ROOT}")
    message(STATUS "MoltenVK Root: ${MOLTENVK_ROOT}")
endif()
