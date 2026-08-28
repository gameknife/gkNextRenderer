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
    RenderViews
)
if(GK_ENABLE_VITURE)
    list(APPEND gkNextRendererModules NextViture)
endif()

if(ANDROID)
    gk_add_android_application(gkNextRenderer
        SOURCES ${gkNextRendererSources}
        MODULES ${gkNextRendererModules}
    )
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
    # Single source of truth: the target property and the gnb manifest below must
    # agree, otherwise `gnb ios run` reports an identifier the bundle never had.
    #
    # Do not go back to com.tzy.gknext. That identifier was previously shipped
    # from another checkout signed by a different Apple team, and macOS records
    # the identity a bundle identifier is expected to carry. Re-signing it with
    # this team trips an AMFI launch constraint, and the app is refused at spawn
    # ("Launch Constraint Violation", launchd job spawn failed, POSIX 162) even
    # though the signature and provisioning profile are perfectly valid.
    set(GK_IOS_BUNDLE_ID "gknext.renderer" CACHE STRING
        "Bundle identifier for the iOS app; must match an installed provisioning profile")

    set_target_properties(gkNextRenderer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME gkNextRenderer
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GK_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )

    if(IOS_DEVELOPMENT_TEAM)
        set_target_properties(gkNextRenderer PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        )
    else()
        # CI builds have no signing certificate or provisioning profile. Without
        # this Xcode still demands a development identity and fails the build.
        # The resulting bundle compiles and links but cannot be launched.
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
        CONTENT "{\n  \"bundle_path\": \"$<TARGET_BUNDLE_DIR:gkNextRenderer>\",\n  \"bundle_id\": \"${GK_IOS_BUNDLE_ID}\"\n}\n"
    )
endif()
