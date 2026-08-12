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

# --- AMD FidelityFX SDK (FSR 3.1 Vulkan) ---
if(WITH_FIDELITYFX)
    set(FIDELITYFX_SDK_CONTAINER "${CMAKE_SOURCE_DIR}/external/fidelityfx-sdk-1.1.4")
    set(FIDELITYFX_SDK_ROOT "${FIDELITYFX_SDK_CONTAINER}/FidelityFX-SDK-1.1.4")
    if(NOT EXISTS "${FIDELITYFX_SDK_ROOT}/ffx-api/include/ffx_api/ffx_api.h" OR
       NOT EXISTS "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.lib")
        message(FATAL_ERROR "FidelityFX SDK v1.1.4 missing under external/. Run `gnb setup` first.")
    endif()
    set(FIDELITYFX_INCLUDE_DIR "${FIDELITYFX_SDK_ROOT}/ffx-api/include")
    set(FIDELITYFX_LIB "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.lib")
    set(FIDELITYFX_DLL "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll")
    message(STATUS "FidelityFX SDK Root: ${FIDELITYFX_SDK_ROOT}")
endif()

# --- TypeScript Compiler (for QuickJS) ---
if(NOT ANDROID)
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
