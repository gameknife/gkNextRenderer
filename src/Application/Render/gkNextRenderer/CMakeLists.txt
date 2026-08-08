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
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/ios/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME gkNextRenderer
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.tzy.gknext"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )

    if(IOS_SKIP_CODE_SIGN)
        set_target_properties(gkNextRenderer PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER ""
        )
    else()
        # Signing identity is per-developer and must not be committed. Supply it at
        # configure time, e.g.:
        #   cmake --preset ios -DIOS_SKIP_CODE_SIGN=OFF \
        #         -DIOS_DEVELOPMENT_TEAM=ABCDE12345 \
        #         -DIOS_PROVISIONING_PROFILE=my.profile.name
        if(NOT IOS_DEVELOPMENT_TEAM)
            message(WARNING
                "IOS_SKIP_CODE_SIGN=OFF but IOS_DEVELOPMENT_TEAM is empty; "
                "Xcode will not be able to sign gkNextRenderer.")
        endif()
        set_target_properties(gkNextRenderer PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${IOS_CODE_SIGN_IDENTITY}"
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${IOS_PROVISIONING_PROFILE}"
        )
    endif()

    foreach(frameworkName IN ITEMS
        AVFoundation
        CoreAudio
        CoreFoundation
        Foundation
        AudioToolbox
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
            ${PROJECT_BINARY_DIR}/assets
            $<TARGET_BUNDLE_DIR:gkNextRenderer>
        COMMENT "Copying assets to iOS bundle"
    )
endif()
