include_guard(GLOBAL)

option(ENABLE_UNITY_BUILD "Enable unity builds for engine/modules/app targets" ON)
option(GK_FAST_DEV_LINK "Prefer faster MSVC executable links for development builds" ON)

function(gk_enable_unity_build target batchSize)
    if(ENABLE_UNITY_BUILD AND NOT ANDROID)
        set_target_properties(${target} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${batchSize}
        )
    endif()
endfunction()

function(gk_enable_fast_dev_link target)
    if(MSVC AND GK_FAST_DEV_LINK)
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
        endif()
        if(UNIX AND NOT APPLE AND NOT ANDROID)
            target_compile_options(${target} PRIVATE -march=native -mavx)
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
        RmlUi::RmlUi
        Jolt::Jolt
        ${Vulkan_LIBRARIES}
        ${extra_libs}
    )
    gk_link_engine_feature_dependencies(${target})
    add_dependencies(${target} Assets)
endfunction()
