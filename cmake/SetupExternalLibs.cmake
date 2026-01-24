include(FetchContent)

# Set base directory for external dependencies to avoid per-preset duplication
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external")
set(FETCHCONTENT_QUIET FALSE)

# --- NVIDIA Streamline ---
if(WITH_STREAMLINE)
    message(STATUS "Preparing NVIDIA Streamline SDK...")
    FetchContent_Declare(
        streamline
        URL https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.0/streamline-sdk-v2.10.0.zip
    )
    FetchContent_MakeAvailable(streamline)

    # Streamline SDK usually extracts directly or into a subfolder.
    # We check if 'include' is in the root of the source dir.
    if(EXISTS "${streamline_SOURCE_DIR}/include/sl.h")
        set(STREAMLINE_ROOT "${streamline_SOURCE_DIR}")
    else()
        # Fallback: Check if there's a single directory inside
        file(GLOB CHILDREN RELATIVE "${streamline_SOURCE_DIR}" "${streamline_SOURCE_DIR}/*")
        list(LENGTH CHILDREN NUM_CHILDREN)
        if(NUM_CHILDREN EQUAL 1)
            list(GET CHILDREN 0 FIRST_CHILD)
            set(STREAMLINE_ROOT "${streamline_SOURCE_DIR}/${FIRST_CHILD}")
        else()
             set(STREAMLINE_ROOT "${streamline_SOURCE_DIR}")
        endif()
    endif()

    set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_ROOT}/include")
    set(STREAMLINE_LIB_DIR "${STREAMLINE_ROOT}/lib/x64")
    set(STREAMLINE_BIN_DIR "${STREAMLINE_ROOT}/bin/x64")

    message(STATUS "Streamline Root: ${STREAMLINE_ROOT}")
    message(STATUS "Streamline Include: ${STREAMLINE_INCLUDE_DIR}")
endif()

# --- Intel OIDN ---
if(WITH_OIDN)
    message(STATUS "Preparing Intel OIDN...")
    
    if(WIN32)
        set(OIDN_URL "https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x64.windows.zip")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
             set(OIDN_URL "https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.arm64.macos.tar.gz")
        else()
             set(OIDN_URL "https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x86_64.macos.tar.gz")
        endif()
    elseif(UNIX AND NOT APPLE) # Linux
         set(OIDN_URL "https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x86_64.linux.tar.gz")
    endif()

    FetchContent_Declare(
        oidn
        URL ${OIDN_URL}
    )
    FetchContent_MakeAvailable(oidn)

    # OIDN zips usually contain a versioned folder
    file(GLOB OIDN_SUBDIRS "${oidn_SOURCE_DIR}/oidn-*")
    list(LENGTH OIDN_SUBDIRS NUM_SUBDIRS)
    if(NUM_SUBDIRS GREATER 0)
        # Sort to ensure deterministic selection if multiple (unlikely)
        list(SORT OIDN_SUBDIRS)
        list(GET OIDN_SUBDIRS 0 OIDN_ROOT)
    else()
        set(OIDN_ROOT "${oidn_SOURCE_DIR}")
    endif()

    set(OIDN_INCLUDE_DIR "${OIDN_ROOT}/include")
    
    if(WIN32)
        set(OIDN_LIB_DIR "${OIDN_ROOT}/lib")
        set(OIDN_BIN_DIR "${OIDN_ROOT}/bin")
    else()
        set(OIDN_LIB_DIR "${OIDN_ROOT}/lib")
        # On Linux/Mac binaries/libs layout can vary slightly but usually:
        # lib/libOpenImageDenoise.so
        # bin/ (executables if any)
        set(OIDN_BIN_DIR "${OIDN_ROOT}/bin")
    endif()
    
    message(STATUS "OIDN Root: ${OIDN_ROOT}")
    message(STATUS "OIDN Include: ${OIDN_INCLUDE_DIR}")
endif()

# --- TypeScript Compiler (for QuickJS) ---
if(WITH_QUICKJS)
    message(STATUS "Preparing TypeScript Compiler...")

    if(CMAKE_HOST_WIN32)
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-windows-amd64.exe")
        set(TSC_FILENAME "tsc.exe")
    elseif(CMAKE_HOST_APPLE)
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-darwin-arm64")
        set(TSC_FILENAME "tsc")
    elseif(CMAKE_HOST_UNIX) # Linux
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-linux-amd64")
        set(TSC_FILENAME "tsc")
    endif()

    FetchContent_Declare(
        tsc
        URL ${TSC_URL}
        DOWNLOAD_NO_EXTRACT TRUE
    )
    FetchContent_MakeAvailable(tsc)

    set(TSC_EXECUTABLE "${tsc_SOURCE_DIR}/${TSC_FILENAME}")
    
    # On non-Windows host, we need to ensure the file is executable
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${TSC_EXECUTABLE}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()

    message(STATUS "TSC Executable: ${TSC_EXECUTABLE}")
endif()
