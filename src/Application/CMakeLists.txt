set(GK_DESKTOP_MAIN_SOURCE ${GK_SOURCE_ROOT}/DesktopMain.cpp)

file(GLOB_RECURSE GK_APPLICATION_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.h"
)

if(ANDROID OR IOS)
    # A mobile build packages exactly one application, so only that application's directory is
    # added. Which one comes from GK_MOBILE_APP: the Android driver passes the target Gradle is
    # about to build, and `gnb ios build --app` passes the one it is about to install. Empty means
    # the first application the registry lists for this platform.
    #
    # Adding one leaf directory is also why each application directory has to stand on its own:
    # nothing above it runs, so an application cannot rely on a variable a parent CMakeLists sets.
    set(GK_MOBILE_APP "" CACHE STRING "Application packaged by this mobile build")

    if(ANDROID)
        set(gkMobilePlatform "android")
        set(gkMobileCandidates ${GK_ANDROID_APPLICATIONS})
    else()
        set(gkMobilePlatform "ios")
        set(gkMobileCandidates ${GK_IOS_APPLICATIONS})
    endif()
    set_property(CACHE GK_MOBILE_APP PROPERTY STRINGS ${gkMobileCandidates})
    gk_resolve_mobile_application("${gkMobilePlatform}" "${GK_MOBILE_APP}" gkMobileSelectedApp)

    if(GK_MOBILE_APP_${gkMobileSelectedApp}_REQUIRES_DOTNET AND NOT GK_DOTNET_ENABLED)
        message(FATAL_ERROR
            "${gkMobileSelectedApp} hosts C#, but the managed scripting layer is disabled for this "
            "configure. See the earlier '.NET scripting disabled' message for the reason.")
    endif()

    message(STATUS "Mobile application: ${gkMobileSelectedApp} "
        "(${GK_MOBILE_APP_${gkMobileSelectedApp}_DIRECTORY})")
    add_subdirectory("${GK_MOBILE_APP_${gkMobileSelectedApp}_DIRECTORY}")
else()
    add_subdirectory(Render)
    add_subdirectory(Editor)
    add_subdirectory(Game)
    add_subdirectory(Util)
endif()
