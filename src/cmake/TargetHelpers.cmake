include_guard(GLOBAL)

option(ENABLE_UNITY_BUILD "Enable unity builds for engine/modules/app targets" ON)
option(GK_FAST_DEV_LINK "Prefer faster MSVC executable links for development builds" ON)
option(GK_NATIVE_ARCH "Compile for the build machine's CPU (-march=native); never use for distributable builds" OFF)
set(GK_ARCH_BASELINE "x86-64-v2" CACHE STRING "Baseline x86 instruction set for non-native Linux builds")

function(gk_enable_unity_build target batchSize)
    if(ENABLE_UNITY_BUILD AND NOT ANDROID)
        set_target_properties(${target} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${batchSize}
        )
    endif()
endfunction()

function(gk_enable_fast_dev_link target)
    if(MSVC AND GK_FAST_DEV_LINK AND NOT GK_ENABLE_ASAN)
        target_link_options(${target} PRIVATE
            /INCREMENTAL
            /OPT:NOREF
            /OPT:NOICF
            /cgthreads:8
        )
        target_compile_options(${target} PRIVATE
            /Zf
        )
    endif()
endfunction()

function(gk_copy_asan_runtime target)
    if(MSVC AND GK_ENABLE_ASAN)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${GK_ASAN_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying MSVC AddressSanitizer runtime for ${target}"
        )
    endif()
endfunction()

function(gk_enable_cpp_live_coding_link target)
    if(MSVC AND GK_ENABLE_CPP_LIVE_CODING)
        target_link_options(${target} PRIVATE
            /FUNCTIONPADMIN
            /INCREMENTAL
            /OPT:NOREF
            /OPT:NOICF
            /DEBUG:FULL
        )
    endif()
endfunction()

function(gk_enable_minimal_size_link target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            /INCREMENTAL:NO
            /OPT:REF
            /OPT:ICF
        )
    endif()
endfunction()

function(gk_apply_target_defaults target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Cannot configure missing target '${target}'")
    endif()

    set_target_properties(${target} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})

    get_target_property(targetType ${target} TYPE)
    if(targetType STREQUAL "EXECUTABLE")
        gk_copy_asan_runtime(${target})
    endif()

    target_include_directories(${target} PRIVATE
        ${GK_SOURCE_ROOT}
        ${STB_INCLUDE_DIRS}
        ${Vulkan_INCLUDE_DIRS}
        ${TINYGLTF_INCLUDE_DIRS}
        ${CPP_BASE64_INCLUDE_DIRS}
        ${GK_SOURCE_ROOT}/ThirdParty/ozz/include
        ${GK_SOURCE_ROOT}/ThirdParty/ImAnim
    )

    target_compile_definitions(${target} PUBLIC
        IMGUI_DEFINE_MATH_OPERATORS
        MA_NO_ENCODING
        MA_NO_FLAC
        GK_ENABLE_HOT_RELOAD=$<BOOL:${GK_ENABLE_HOT_RELOAD}>
        GK_ENABLE_CPP_LIVE_CODING=$<BOOL:${GK_ENABLE_CPP_LIVE_CODING}>
        GK_ENABLE_SHADER_CLOCK=$<BOOL:${GK_ENABLE_SHADER_CLOCK}>
        GK_WITH_TUI=$<BOOL:${GK_WITH_TUI}>
        GK_WITH_RMLUI=1
        GK_WITH_REMOTE=$<BOOL:${GK_REMOTE_ENABLED}>
        GK_WITH_VITURE=$<BOOL:${GK_ENABLE_VITURE}>
    )

    if(WIN32)
        target_compile_definitions(${target} PUBLIC
            UNICODE
            _UNICODE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            VK_USE_PLATFORM_WIN32_KHR
            PLATFORM__WINDOWS
        )
    elseif(UNIX AND NOT ANDROID)
        target_compile_definitions(${target} PUBLIC UNIX)
        target_compile_options(${target} PRIVATE -fvisibility=hidden)
    endif()

    if(IOS)
        target_compile_definitions(${target} PUBLIC IOS=1)
    endif()

    if(APPLE AND GK_HAS_NO_WARN_DUPLICATE_LIBRARIES)
        target_link_options(${target} PRIVATE -Wl,-no_warn_duplicate_libraries)
    endif()

    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        target_compile_definitions(${target} PRIVATE
            JSON_HAS_FILESYSTEM=0
            JSON_HAS_EXPERIMENTAL_FILESYSTEM=0
        )
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /MP /utf-8 /WX /wd4200 /Zf)
        if(GK_ENABLE_CPP_LIVE_CODING)
            target_compile_options(${target} PRIVATE /Zi /Gm- /Gy /Gw)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Werror
            -Wno-unused-but-set-variable
            -Wno-inconsistent-missing-override
            -Wno-unused-const-variable
            -Wno-unused-variable
            -Wno-unused-private-field
            -Wno-unused-function
            -Wno-sign-compare
            -Wno-unused-result
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Wno-unknown-warning-option)
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wno-stringop-overflow
                -Wno-ignored-attributes
            )
            if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
                # GCC 16 reports a false positive in nlohmann/json's shared
                # output adapter destruction when optimization is enabled.
                target_compile_options(${target} PRIVATE -Wno-array-bounds)
            endif()
        endif()
        if(UNIX AND NOT APPLE AND NOT ANDROID AND
           CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            # Distributable builds must run on any CPU meeting the documented baseline.
            # -march=native bakes in the build machine's instruction set and makes the
            # binary SIGILL on older CPUs, so it stays opt-in for local development.
            if(GK_NATIVE_ARCH)
                target_compile_options(${target} PRIVATE -march=native -mavx)
            else()
                target_compile_options(${target} PRIVATE -march=${GK_ARCH_BASELINE})
            endif()
        elseif(UNIX AND NOT APPLE AND NOT ANDROID AND GK_NATIVE_ARCH)
            target_compile_options(${target} PRIVATE -march=native)
        endif()
    endif()
endfunction()

function(gk_configure_module target)
    gk_apply_target_defaults(${target})
    gk_enable_unity_build(${target} 8)
    set_target_properties(${target} PROPERTIES FOLDER "Modules")
    target_link_libraries(${target} PRIVATE gkNextEngine)

    string(TOUPPER "${target}" moduleUpper)
    target_compile_definitions(${target} INTERFACE "GK_MODULE_${moduleUpper}=1")
endfunction()

function(gk_target_runtime_modules target)
    cmake_parse_arguments(ARG "" "" "MODULES" ${ARGN})
    if(NOT TARGET ${target})
        return()
    endif()

    foreach(module IN LISTS ARG_MODULES)
        if(NOT module IN_LIST GK_MODULE_NAMES)
            message(FATAL_ERROR "Unknown gk runtime module '${module}' for target '${target}'")
        endif()
        if(TARGET ${module})
            target_link_libraries(${target} PRIVATE ${module})
        elseif(ANDROID)
            string(TOUPPER "${module}" moduleUpper)
            target_compile_definitions(${target} PRIVATE "GK_MODULE_${moduleUpper}=1")
        else()
            message(FATAL_ERROR "Runtime module target '${module}' is unavailable for '${target}'")
        endif()
        set_property(TARGET ${target} APPEND PROPERTY GK_RUNTIME_MODULES "${module}")

        if(module STREQUAL "NextViture")
            if(NOT TARGET gkNextVitureRuntime)
                set(gkVitureRuntimeGlasses "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libglasses.dylib")
                set(gkVitureRuntimeVio "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libcarina_vio.dylib")
                add_custom_command(
                    OUTPUT "${gkVitureRuntimeGlasses}" "${gkVitureRuntimeVio}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GK_VITURE_SDK_LIBRARY}" "${gkVitureRuntimeGlasses}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GK_VITURE_VIO_LIBRARY}" "${gkVitureRuntimeVio}"
                    # The SDK's libglasses.dylib currently carries an invalid
                    # embedded signature. macOS rejects it only once --ar causes
                    # dyld to load it, so repair the copied development runtime.
                    COMMAND "${GK_VITURE_CODESIGN_EXECUTABLE}" --force --sign - "${gkVitureRuntimeGlasses}"
                    DEPENDS "${GK_VITURE_ARCHIVE}" "${GK_VITURE_SDK_LIBRARY}" "${GK_VITURE_VIO_LIBRARY}"
                    COMMENT "Preparing VITURE XR runtime"
                    VERBATIM
                )
                add_custom_target(gkNextVitureRuntime DEPENDS
                    "${gkVitureRuntimeGlasses}" "${gkVitureRuntimeVio}")
                set_target_properties(gkNextVitureRuntime PROPERTIES FOLDER "Modules")
            endif()
            add_dependencies(${target} gkNextVitureRuntime)
        endif()
    endforeach()
endfunction()

function(gk_configure_application target)
    cmake_parse_arguments(ARG "CORE_ONLY;NO_UNITY;NO_FAST_LINK;MINIMAL_LINK" "" "MODULES" ${ARGN})

    gk_apply_target_defaults(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Programs")
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(${target} Assets)

    if(MSVC)
        set(gkFfmpegPath "${GK_SOURCE_ROOT}/ThirdParty/ffmpeg/bin/ffmpeg.exe")
        if(EXISTS "${gkFfmpegPath}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${gkFfmpegPath}
                    $<TARGET_FILE_DIR:${target}>/ffmpeg.exe
                COMMENT "Copying ffmpeg.exe for ${target}")
        endif()
    endif()

    if(NOT ANDROID)
        target_link_libraries(${target} PRIVATE gkNextEngine)
    endif()

    # Release archives place the macOS Vulkan loader next to each executable.
    # Keep this relocatable rpath in addition to CMake's SDK build rpath.
    if(APPLE AND NOT IOS)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path")
    endif()

    if(NOT ARG_NO_UNITY)
        gk_enable_unity_build(${target} 8)
    endif()
    if(ARG_MINIMAL_LINK)
        gk_enable_minimal_size_link(${target})
    elseif(NOT ARG_NO_FAST_LINK)
        gk_enable_fast_dev_link(${target})
    endif()

    if(ARG_MODULES)
        gk_target_runtime_modules(${target} MODULES ${ARG_MODULES})
    endif()

    get_target_property(runtimeModules ${target} GK_RUNTIME_MODULES)
    if(runtimeModules AND "LiveCoding" IN_LIST runtimeModules)
        gk_enable_cpp_live_coding_link(${target})
    endif()

    if(ARG_CORE_ONLY)
        get_target_property(coreOnlyModules ${target} GK_RUNTIME_MODULES)
        if(coreOnlyModules)
            message(FATAL_ERROR "${target} must remain core-only, got: ${coreOnlyModules}")
        endif()
    endif()
endfunction()

function(gk_link_engine_feature_dependencies target)
    if(WITH_SUPERLUMINAL)
        target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=1)
        target_include_directories(${target} SYSTEM PUBLIC ${SuperluminalAPI_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE SuperluminalAPI)
        if(MSVC)
            target_link_options(${target} PRIVATE /ignore:4099)
        endif()
    else()
        target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=0)
    endif()

    if(GK_ENABLE_TRACY AND TARGET Tracy::TracyClient)
        # TracyIntegration.hpp is visible through Engine headers and can be
        # parsed by modules as well as the engine target. Keep Tracy's header
        # mode consistent for every consumer, not only the library that owns
        # the client link.
        target_compile_definitions(${target} PUBLIC
            GK_TRACY_ENABLED=1
            TRACY_ENABLE
            TRACY_ON_DEMAND
            TRACY_NO_CRASH_HANDLER
        )
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
    else()
        target_compile_definitions(${target} PUBLIC GK_TRACY_ENABLED=0)
    endif()

    if(WITH_RENDERDOC)
        target_compile_definitions(${target} PUBLIC WITH_RENDERDOC=1)
        if(GK_RENDERDOC_ANDROID)
            target_compile_definitions(${target} PUBLIC GK_RENDERDOC_ANDROID=1)
        else()
            target_compile_definitions(${target} PUBLIC
                GK_RENDERDOC_ANDROID=0
                GK_RENDERDOC_DLL_PATH="${GK_RENDERDOC_DLL_PATH}"
            )
        endif()
        target_include_directories(${target} SYSTEM PUBLIC ${GK_RENDERDOC_API_DIR})
    else()
        target_compile_definitions(${target} PUBLIC WITH_RENDERDOC=0 GK_RENDERDOC_ANDROID=0)
    endif()

    target_link_libraries(${target} PRIVATE ozz KTX::ktx)

    if(WITH_AVIF)
        target_compile_definitions(${target} PUBLIC WITH_AVIF=1 AVIF_CODEC_AOM=SYSTEM)
        target_include_directories(${target} PUBLIC
            $<TARGET_PROPERTY:avif,INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_link_libraries(${target} PRIVATE avif)
    endif()
endfunction()

function(gk_configure_android_runtime target)
    find_package(WebP CONFIG REQUIRED)
    find_package(RmlUi CONFIG REQUIRED)
    find_package(Jolt CONFIG REQUIRED)

    gk_apply_target_defaults(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Programs")
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_PROPERTY:Jolt::Jolt,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_options(${target} PRIVATE
        -Wl,-z,max-page-size=16384
        -Wl,-z,common-page-size=16384
        -Wl,--hash-style=gnu
    )
    target_link_libraries(${target} PRIVATE
        android
        SDL3::SDL3
        spdlog::spdlog
        xxHash::xxhash
        meshoptimizer::meshoptimizer
        fmt::fmt
        CURL::libcurl
        glm::glm
        imgui::imgui
        draco::draco
        WebP::webp
        WebP::libwebpmux
        RmlUi::RmlUi
        Jolt::Jolt
        ${Vulkan_LIBRARIES}
        ${extra_libs}
    )
    gk_link_engine_feature_dependencies(${target})
    add_dependencies(${target} Assets)
endfunction()

# Builds one Android application library. Called through gk_add_application, never directly.
#
# Android has no static engine libraries: src/Engine, src/Modules and src/Gameplay are compiled
# straight into the application's .so (see src/CMakeLists.txt), so every APK owns a full copy of
# the engine and exactly one application is built per Gradle invocation. That is why the whole
# source list is repeated per target instead of being linked, and why this function exists.
#
#   gk_add_android_application(<target> SOURCES <application sources> MODULES <runtime modules>
#                              [ENTRY <entry point source>])
function(gk_add_android_application target)
    cmake_parse_arguments(ARG "" "ENTRY" "SOURCES;MODULES" ${ARGN})

    # Unrequested modules are normally compiled in and left inert behind their GK_MODULE_* define.
    # NextDotNet cannot be: its CoreCLR host includes hostfxr headers that only a .NET installation
    # provides, so an application that does not host C# must not compile it at all.
    set(moduleSources ${src_files_modules_all})
    if(GK_DOTNET_ENABLED AND NOT "NextDotNet" IN_LIST ARG_MODULES)
        list(REMOVE_ITEM moduleSources ${src_files_module_NextDotNet})
    endif()

    add_library(${target} SHARED
        ${src_files_assets}
        ${src_files_utilities}
        ${src_files_engine}
        ${src_files_vulkan}
        ${src_files_rendering}
        ${src_files_nextgameplay}
        ${src_files_thirdparty}
        ${moduleSources}
        ${ARG_SOURCES}
        ${ARG_ENTRY}
    )
    gk_configure_android_runtime(${target})
    gk_target_runtime_modules(${target} MODULES ${ARG_MODULES})

    # On desktop this comes from the NextDotNet module target, which Android does not build. It
    # must not be missed: without it the sources fall back to the CoreCLR host, which needs
    # hostfxr.h and a runtime the device does not have.
    if(GK_DOTNET_ENABLED AND "NextDotNet" IN_LIST ARG_MODULES)
        target_compile_definitions(${target} PRIVATE GK_DOTNET_USE_AOT=${GK_DOTNET_USE_AOT})
    endif()
endfunction()

# Turns an executable into a launchable iOS application bundle. Called through gk_add_application,
# never directly.
#
# Every identity here comes from the mobile application registry, so the bundle identifier CMake
# stamps on the target and the one `gnb ios run` reads back out of the generated manifest cannot
# drift apart.
#
# Do not hand any application the identifier com.tzy.gknext. It was previously shipped from another
# checkout signed by a different Apple team, and macOS records the identity a bundle identifier is
# expected to carry. Re-signing it with this team trips an AMFI launch constraint and the app is
# refused at spawn ("Launch Constraint Violation", launchd job spawn failed, POSIX 162) even though
# the signature and provisioning profile are perfectly valid.
function(gk_configure_ios_application target)
    set(bundleId "${GK_MOBILE_APP_${target}_IOS_BUNDLE_ID}")
    set(bundleLabel "${GK_MOBILE_APP_${target}_LABEL}")
    if(NOT bundleId OR NOT bundleLabel)
        message(FATAL_ERROR
            "${target} has no iOS identity. Add it to ${GK_MOBILE_APPLICATIONS_MANIFEST}.")
    endif()

    # An application may ship its own Info.plist; everything else gets the shared one, named after
    # itself. GK_IOS_BUNDLE_DISPLAY_NAME is what the template substitutes.
    set(infoPlist "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
    if(NOT EXISTS "${infoPlist}")
        set(GK_IOS_BUNDLE_DISPLAY_NAME "${bundleLabel}")
        set(infoPlist "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
        configure_file("${GK_SOURCE_ROOT}/Application/Info.plist.in" "${infoPlist}" @ONLY)
    endif()

    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${infoPlist}"
        MACOSX_BUNDLE_BUNDLE_NAME "${target}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${bundleId}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )

    if(IOS_DEVELOPMENT_TEAM)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        )
    else()
        # CI builds have no signing certificate or provisioning profile. Without this Xcode still
        # demands a development identity and fails the build. The resulting bundle compiles and
        # links but cannot be launched.
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        )
    endif()

    foreach(frameworkName IN ITEMS
        AVFoundation
        CoreAudio
        CoreFoundation
        Foundation
        AudioToolbox
        ImageIO
        Metal
        QuartzCore
        CoreGraphics
        IOSurface
        UIKit
    )
        find_library(${frameworkName}_FRAMEWORK ${frameworkName} REQUIRED)
        target_link_libraries(${target} PRIVATE ${${frameworkName}_FRAMEWORK})
    endforeach()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_BINARY_DIR}/assets/assets
            $<TARGET_BUNDLE_DIR:${target}>/assets
        COMMENT "Copying assets to iOS bundle"
    )

    # What `gnb ios run` installs and launches. Exactly one application is configured per iOS build
    # tree, so this manifest names it without any further selection.
    file(GENERATE
        OUTPUT "${PROJECT_BINARY_DIR}/ios-app-$<CONFIG>.json"
        CONTENT "{\n  \"app\": \"${target}\",\n  \"bundle_path\": \"$<TARGET_BUNDLE_DIR:${target}>\",\n  \"bundle_id\": \"${bundleId}\"\n}\n"
    )
endfunction()

# Applies the parts of a mobile application that are the same on both platforms. It has no command
# line, so its identity is compiled in; its initial scene remains application-owned.
function(gk_configure_mobile_application target)
    if(NOT target IN_LIST GK_MOBILE_APPLICATIONS)
        message(FATAL_ERROR
            "${target} is not a mobile application. Add it to ${GK_MOBILE_APPLICATIONS_MANIFEST}.")
    endif()

    target_compile_definitions(${target} PRIVATE GK_APPLICATION_NAME="${target}")

    if(IOS)
        gk_configure_ios_application(${target})
    endif()
endfunction()

# Declares one application, on whichever platform is being configured.
#
# This is the only way an application should be created. The three platforms disagree about what an
# application even is — a desktop executable, an iOS bundle, or an Android shared library carrying
# its own copy of the engine — and routing every application through here is what lets the mobile
# registry package any of them instead of the one that happened to have a hand-written Android
# branch.
#
#   gk_add_application(<target>
#       SOURCES  <application sources>
#       [MODULES <runtime modules>]   Optional engine modules, resolved per platform.
#       [LINK    <libraries>]         Engine libraries (NextGameplay, ...) that Android compiles
#                                     into the application instead of linking.
#       [DEFINES <definitions>]       PRIVATE compile definitions.
#       [INCLUDES <directories>]      PRIVATE include directories.
#       [NO_DEFAULT_MAIN]             The application brings its own SDL entry point.
#       [CORE_ONLY] [NO_UNITY] [NO_FAST_LINK] [MINIMAL_LINK])
function(gk_add_application target)
    cmake_parse_arguments(ARG
        "CORE_ONLY;NO_UNITY;NO_FAST_LINK;MINIMAL_LINK;NO_DEFAULT_MAIN"
        ""
        "SOURCES;MODULES;LINK;DEFINES;INCLUDES"
        ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "gk_add_application(${target}) requires SOURCES")
    endif()

    set(entryPoint "")
    if(NOT ARG_NO_DEFAULT_MAIN)
        if(ANDROID)
            set(entryPoint "${GK_SOURCE_ROOT}/AndroidMain.cpp")
        else()
            set(entryPoint "${GK_DESKTOP_MAIN_SOURCE}")
        endif()
    endif()

    if(ANDROID)
        gk_add_android_application(${target}
            SOURCES ${ARG_SOURCES}
            ENTRY ${entryPoint}
            MODULES ${ARG_MODULES}
        )
    else()
        set(bundleType "")
        if(IOS)
            set(bundleType MACOSX_BUNDLE)
        endif()
        add_executable(${target} ${bundleType} ${ARG_SOURCES} ${entryPoint})

        set(configureOptions "")
        foreach(optionName IN ITEMS CORE_ONLY NO_UNITY NO_FAST_LINK MINIMAL_LINK)
            if(ARG_${optionName})
                list(APPEND configureOptions ${optionName})
            endif()
        endforeach()
        gk_configure_application(${target} ${configureOptions} MODULES ${ARG_MODULES})
    endif()

    # Engine libraries only exist where the engine is a library. Android compiles the same sources
    # straight into the application, so there is nothing left to link there.
    foreach(library IN LISTS ARG_LINK)
        if(TARGET ${library})
            target_link_libraries(${target} PRIVATE ${library})
        elseif(NOT ANDROID)
            message(FATAL_ERROR
                "gk_add_application(${target}) links '${library}', which is not a target here")
        endif()
    endforeach()

    if(ARG_DEFINES)
        target_compile_definitions(${target} PRIVATE ${ARG_DEFINES})
    endif()
    if(ARG_INCLUDES)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDES})
    endif()

    if(ANDROID OR IOS)
        gk_configure_mobile_application(${target})
    endif()
endfunction()
