file(GLOB_RECURSE gkNextRendererSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

set(gkNextRendererModules
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
    SplatLoader
)

if(ANDROID)
    add_library(gkNextRenderer SHARED
        ${src_files_assets}
        ${src_files_utilities}
        ${src_files_engine}
        ${src_files_vulkan}
        ${src_files_rendering}
        ${src_files_nextgameplay}
        ${src_files_thirdparty}
        ${src_files_modules_all}
        ${GK_APPLICATION_COMMON_SOURCES}
        ${gkNextRendererSources}
        ${GK_SOURCE_ROOT}/AndroidMain.cpp
    )
    gk_configure_android_runtime(gkNextRenderer)
    gk_target_runtime_modules(gkNextRenderer MODULES ${gkNextRendererModules})
else()
    if(IOS)
        add_executable(gkNextRenderer MACOSX_BUNDLE
            ${gkNextRendererSources}
            ${GK_APPLICATION_COMMON_SOURCES}
            ${GK_DESKTOP_MAIN_SOURCE}
        )
    else()
        add_executable(gkNextRenderer
            ${gkNextRendererSources}
            ${GK_APPLICATION_COMMON_SOURCES}
            ${GK_DESKTOP_MAIN_SOURCE}
        )
    endif()

    gk_configure_application(gkNextRenderer MODULES
        ${gkNextRendererModules}
    )
    target_link_libraries(gkNextRenderer PRIVATE NextGameplay)
endif()

if(IOS)
    set_target_properties(gkNextRenderer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME gkNextRenderer
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.tzy.gknext"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )

    if(IOS_DEVELOPMENT_TEAM)
        set_target_properties(gkNextRenderer PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        )
    else()
        set_target_properties(gkNextRenderer PROPERTIES
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
        target_link_libraries(gkNextRenderer PRIVATE ${${frameworkName}_FRAMEWORK})
    endforeach()

    add_custom_command(TARGET gkNextRenderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_BINARY_DIR}/assets/assets
            $<TARGET_BUNDLE_DIR:gkNextRenderer>/assets
        COMMENT "Copying assets to iOS bundle"
    )

    file(GENERATE
        OUTPUT "${PROJECT_BINARY_DIR}/ios-app-$<CONFIG>.json"
        CONTENT "{\n  \"bundle_path\": \"$<TARGET_BUNDLE_DIR:gkNextRenderer>\",\n  \"bundle_id\": \"com.tzy.gknext\"\n}\n"
    )
endif()
