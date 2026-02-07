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
    set(TSC_VERSION "v2025.5.23")

    if(CMAKE_HOST_WIN32)
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/${TSC_VERSION}/tsgo-windows-amd64.exe")
        set(TSC_FILENAME "tsc.exe")
    elseif(CMAKE_HOST_APPLE)
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/${TSC_VERSION}/tsgo-darwin-arm64")
        set(TSC_FILENAME "tsc")
    elseif(CMAKE_HOST_UNIX) # Linux
        set(TSC_URL "https://github.com/rxliuli/tsgo-npm-release/releases/download/${TSC_VERSION}/tsgo-linux-amd64")
        set(TSC_FILENAME "tsc")
    endif()

    set(TSC_CACHE_DIR "${FETCHCONTENT_BASE_DIR}/tsc-src")
    set(TSC_VERSION_FILE "${TSC_CACHE_DIR}/.tsc_version")
    set(TSC_EXECUTABLE "${TSC_CACHE_DIR}/${TSC_FILENAME}")
    set(TSC_CACHE_VALID FALSE)

    if(EXISTS "${TSC_EXECUTABLE}")
        if(EXISTS "${TSC_VERSION_FILE}")
            file(READ "${TSC_VERSION_FILE}" TSC_CACHED_VERSION)
            string(STRIP "${TSC_CACHED_VERSION}" TSC_CACHED_VERSION)
            if(TSC_CACHED_VERSION STREQUAL TSC_VERSION)
                set(TSC_CACHE_VALID TRUE)
            endif()
        else()
            # Backward compatibility: old cache has no version marker.
            set(TSC_CACHE_VALID TRUE)
            file(WRITE "${TSC_VERSION_FILE}" "${TSC_VERSION}\n")
        endif()
    endif()

    if(TSC_CACHE_VALID)
        message(STATUS "Using cached TypeScript Compiler: ${TSC_EXECUTABLE} (${TSC_VERSION})")
    else()
        FetchContent_Declare(
            tsc
            URL ${TSC_URL}
            DOWNLOAD_NO_EXTRACT TRUE
        )
        FetchContent_MakeAvailable(tsc)

        # Rename the downloaded file to the expected TSC_FILENAME
        get_filename_component(TSC_DOWNLOADED_NAME "${TSC_URL}" NAME)
        if(EXISTS "${tsc_SOURCE_DIR}/${TSC_DOWNLOADED_NAME}")
            if(EXISTS "${TSC_EXECUTABLE}")
                file(REMOVE "${TSC_EXECUTABLE}")
            endif()
            file(RENAME "${tsc_SOURCE_DIR}/${TSC_DOWNLOADED_NAME}" "${TSC_EXECUTABLE}")
        endif()

        if(NOT EXISTS "${TSC_EXECUTABLE}")
            message(FATAL_ERROR "Failed to prepare TypeScript Compiler at ${TSC_EXECUTABLE}")
        endif()

        file(WRITE "${TSC_VERSION_FILE}" "${TSC_VERSION}\n")
    endif()
    
    # On non-Windows host, we need to ensure the file is executable
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${TSC_EXECUTABLE}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()

    message(STATUS "TSC Executable: ${TSC_EXECUTABLE}")
endif()

# --- MoltenVK (iOS only) ---
if(IOS)
    message(STATUS "Preparing MoltenVK for iOS...")
    set(MVK_VERSION "v1.4.0")
    FetchContent_Declare(
        moltenvk
        URL "https://github.com/KhronosGroup/MoltenVK/releases/download/${MVK_VERSION}/MoltenVK-ios.tar"
    )
    FetchContent_MakeAvailable(moltenvk)

    # The tarball structure usually contains a top-level MoltenVK folder.
    # We need to locate the static library for iOS arm64.
    set(MVK_SOURCE_PATH "${moltenvk_SOURCE_DIR}/MoltenVK/static/MoltenVK.xcframework/ios-arm64")
    
    # Check if the path exists (tar structure usually matches this path)
    if(EXISTS "${MVK_SOURCE_PATH}/libMoltenVK.a")
        # SetupDependencies.cmake expects ${MOLTENVK_ROOT}/lib/libMoltenVK.a
        file(MAKE_DIRECTORY "${moltenvk_SOURCE_DIR}/lib")
        configure_file("${MVK_SOURCE_PATH}/libMoltenVK.a" "${moltenvk_SOURCE_DIR}/lib/libMoltenVK.a" COPYONLY)
        
        set(MOLTENVK_ROOT "${moltenvk_SOURCE_DIR}")
        message(STATUS "MoltenVK Root: ${MOLTENVK_ROOT}")
    else()
        message(WARNING "Could not locate libMoltenVK.a in expected path: ${MVK_SOURCE_PATH}")
    endif()
endif()
