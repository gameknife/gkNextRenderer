cmake_minimum_required(VERSION 3.26)

project(gkNextAndroidDriver LANGUAGES NONE)

set(GK_ANDROID_VARIANT "release" CACHE STRING "Android build variant (relwithdebinfo, debug, or release)")
set_property(CACHE GK_ANDROID_VARIANT PROPERTY STRINGS relwithdebinfo debug release)
set(GK_ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")
set(GK_ANDROID_APP "" CACHE STRING "Application target packaged into the APK")
set(GK_ANDROID_SDK_ROOT "" CACHE PATH "Android SDK root")
set(GK_ANDROID_NDK_ROOT "" CACHE PATH "Android NDK root")
set(GK_ANDROID_JAVA_HOME "" CACHE PATH "JDK root")
set(GK_ANDROID_SERIAL "" CACHE STRING "adb device serial")
set(GK_ANDROID_SIGNING_PROPERTIES "" CACHE FILEPATH "External Gradle properties file for release signing")

if(NOT GK_ANDROID_VARIANT MATCHES "^(relwithdebinfo|debug|release)$")
    message(FATAL_ERROR "GK_ANDROID_VARIANT must be 'relwithdebinfo', 'debug', or 'release'")
endif()
if(NOT GK_ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported during the Android build migration")
endif()

get_filename_component(GK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Every application the APK can be built around, and the identity each one installs under, come
# from the same registry the engine configure reads. Hard-coding them here is how the driver and
# the engine used to disagree about which applications exist.
#
# Each application gets its own applicationId so a device can hold more than one of them, but they
# all share the com.gknext.renderer Java package: the activity is identical, and only the native
# library it loads differs. That split is why the manifest names an app id and the activity is
# still addressed as com.gknext.renderer.GkNextActivity.
include("${GK_REPO_ROOT}/src/cmake/MobileApplications.cmake")
set_property(CACHE GK_ANDROID_APP PROPERTY STRINGS ${GK_ANDROID_APPLICATIONS})
gk_resolve_mobile_application("android" "${GK_ANDROID_APP}" gkResolvedAndroidApp)
set(GK_ANDROID_APP "${gkResolvedAndroidApp}")
set(GK_ANDROID_APPLICATION_ID "${GK_MOBILE_APP_${GK_ANDROID_APP}_ANDROID_ID}")
set(GK_ANDROID_APP_LABEL "${GK_MOBILE_APP_${GK_ANDROID_APP}_LABEL}")
set(GK_GRADLE_DIR "${CMAKE_BINARY_DIR}/gradle")
set(GK_NATIVE_DIR "${CMAKE_BINARY_DIR}/native")
set(GK_ASSET_ROOT "${CMAKE_BINARY_DIR}/assets")
set(GK_ANDROID_ASSET_OUTPUT_DIR "${GK_ASSET_ROOT}/assets")
set(GK_JNILIBS_DIR "${CMAKE_BINARY_DIR}/jniLibs")
set(GK_CACHE_DIR "${CMAKE_BINARY_DIR}/cache")
set(GK_APK_DIR "${CMAKE_BINARY_DIR}/apk")

set(GK_ANDROID_HARDWARE_ACCELERATED "false")

if(NOT GK_ANDROID_SDK_ROOT)
    if(DEFINED ENV{ANDROID_SDK_ROOT} AND NOT "$ENV{ANDROID_SDK_ROOT}" STREQUAL "")
        set(GK_ANDROID_SDK_ROOT "$ENV{ANDROID_SDK_ROOT}")
    elseif(DEFINED ENV{ANDROID_HOME} AND NOT "$ENV{ANDROID_HOME}" STREQUAL "")
        set(GK_ANDROID_SDK_ROOT "$ENV{ANDROID_HOME}")
    elseif(CMAKE_HOST_WIN32 AND DEFINED ENV{LOCALAPPDATA})
        set(GK_ANDROID_SDK_ROOT "$ENV{LOCALAPPDATA}/Android/Sdk")
    elseif(CMAKE_HOST_APPLE AND DEFINED ENV{HOME})
        set(GK_ANDROID_SDK_ROOT "$ENV{HOME}/Library/Android/sdk")
    elseif(DEFINED ENV{HOME})
        set(GK_ANDROID_SDK_ROOT "$ENV{HOME}/Android/Sdk")
    endif()
endif()
get_filename_component(GK_ANDROID_SDK_ROOT "${GK_ANDROID_SDK_ROOT}" ABSOLUTE)

function(gk_android_ndk_supports_engine ndkRoot outputVariable)
    file(GLOB libcxxRoots LIST_DIRECTORIES true
        "${ndkRoot}/toolchains/llvm/prebuilt/*/sysroot/usr/include/c++/v1")
    set(isSupported FALSE)
    foreach(libcxxRoot IN LISTS libcxxRoots)
        if(NOT EXISTS "${libcxxRoot}/ranges")
            continue()
        endif()
        set(isSupported TRUE)
        break()
    endforeach()
    set(${outputVariable} ${isSupported} PARENT_SCOPE)
endfunction()

if(NOT GK_ANDROID_NDK_ROOT)
    foreach(envName IN ITEMS ANDROID_NDK_ROOT ANDROID_NDK_HOME)
        if(DEFINED ENV{${envName}} AND NOT "$ENV{${envName}}" STREQUAL "")
            set(GK_ANDROID_NDK_ROOT "$ENV{${envName}}")
            break()
        endif()
    endforeach()
endif()
if(NOT GK_ANDROID_NDK_ROOT)
    file(GLOB ndkCandidates LIST_DIRECTORIES true "${GK_ANDROID_SDK_ROOT}/ndk/*")
    list(SORT ndkCandidates COMPARE NATURAL ORDER DESCENDING)
    foreach(candidate IN LISTS ndkCandidates)
        gk_android_ndk_supports_engine("${candidate}" candidateSupportsEngine)
        if(EXISTS "${candidate}/build/cmake/android.toolchain.cmake" AND candidateSupportsEngine)
            set(GK_ANDROID_NDK_ROOT "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT GK_ANDROID_NDK_ROOT AND EXISTS "${GK_ANDROID_SDK_ROOT}/ndk-bundle/build/cmake/android.toolchain.cmake")
    gk_android_ndk_supports_engine("${GK_ANDROID_SDK_ROOT}/ndk-bundle" bundleSupportsEngine)
    if(bundleSupportsEngine)
        set(GK_ANDROID_NDK_ROOT "${GK_ANDROID_SDK_ROOT}/ndk-bundle")
    endif()
endif()
if(NOT GK_ANDROID_NDK_ROOT)
    message(FATAL_ERROR
        "No compatible Android NDK found. gkNextRenderer requires libc++ support for C++20 "
        "<ranges>; install a current NDK or set GK_ANDROID_NDK_ROOT.")
endif()
get_filename_component(GK_ANDROID_NDK_ROOT "${GK_ANDROID_NDK_ROOT}" ABSOLUTE)
get_filename_component(GK_ANDROID_NDK_VERSION "${GK_ANDROID_NDK_ROOT}" NAME)
gk_android_ndk_supports_engine("${GK_ANDROID_NDK_ROOT}" selectedNdkSupportsEngine)
if(NOT selectedNdkSupportsEngine)
    message(FATAL_ERROR
        "The selected NDK (${GK_ANDROID_NDK_VERSION}) lacks the libc++ C++20 <ranges> support "
        "required by gkNextRenderer. Select a newer NDK with GK_ANDROID_NDK_ROOT.")
endif()

if(NOT GK_ANDROID_JAVA_HOME AND DEFINED ENV{JAVA_HOME} AND NOT "$ENV{JAVA_HOME}" STREQUAL "")
    set(GK_ANDROID_JAVA_HOME "$ENV{JAVA_HOME}")
endif()
if(NOT GK_ANDROID_JAVA_HOME)
    set(javaHomeCandidates "")
    if(CMAKE_HOST_WIN32)
        list(APPEND javaHomeCandidates "C:/Program Files/Android/Android Studio/jbr")
    elseif(CMAKE_HOST_APPLE)
        list(APPEND javaHomeCandidates "/Applications/Android Studio.app/Contents/jbr/Contents/Home")
    else()
        list(APPEND javaHomeCandidates "/opt/android-studio/jbr" "/usr/local/android-studio/jbr")
    endif()
    foreach(candidate IN LISTS javaHomeCandidates)
        if(EXISTS "${candidate}/bin/java${CMAKE_EXECUTABLE_SUFFIX}")
            set(GK_ANDROID_JAVA_HOME "${candidate}")
            break()
        endif()
    endforeach()
endif()
get_filename_component(GK_ANDROID_JAVA_HOME "${GK_ANDROID_JAVA_HOME}" ABSOLUTE)

if(NOT GK_ANDROID_SIGNING_PROPERTIES)
    if(DEFINED ENV{GK_ANDROID_SIGNING_PROPERTIES} AND NOT "$ENV{GK_ANDROID_SIGNING_PROPERTIES}" STREQUAL "")
        set(GK_ANDROID_SIGNING_PROPERTIES "$ENV{GK_ANDROID_SIGNING_PROPERTIES}")
    elseif(CMAKE_HOST_WIN32 AND DEFINED ENV{USERPROFILE}
            AND EXISTS "$ENV{USERPROFILE}/.gknext/android-signing.properties")
        set(GK_ANDROID_SIGNING_PROPERTIES "$ENV{USERPROFILE}/.gknext/android-signing.properties")
    elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.gknext/android-signing.properties")
        set(GK_ANDROID_SIGNING_PROPERTIES "$ENV{HOME}/.gknext/android-signing.properties")
    endif()
endif()
set(GK_ANDROID_RELEASE_SIGNED FALSE)
if(GK_ANDROID_SIGNING_PROPERTIES)
    get_filename_component(GK_ANDROID_SIGNING_PROPERTIES "${GK_ANDROID_SIGNING_PROPERTIES}" ABSOLUTE)
    if(NOT EXISTS "${GK_ANDROID_SIGNING_PROPERTIES}")
        message(FATAL_ERROR "Android signing properties not found: ${GK_ANDROID_SIGNING_PROPERTIES}")
    endif()
    foreach(requiredProperty IN ITEMS storeFile storePassword keyAlias keyPassword)
        file(STRINGS "${GK_ANDROID_SIGNING_PROPERTIES}" propertyLines
            REGEX "^${requiredProperty}=.+$")
        if(NOT propertyLines)
            message(FATAL_ERROR
                "Android signing properties is missing '${requiredProperty}': ${GK_ANDROID_SIGNING_PROPERTIES}")
        endif()
    endforeach()
    set(GK_ANDROID_RELEASE_SIGNED TRUE)
endif()

set(adbSuffix "")
if(CMAKE_HOST_WIN32)
    set(adbSuffix ".exe")
endif()
set(GK_ADB "${GK_ANDROID_SDK_ROOT}/platform-tools/adb${adbSuffix}")
set(GK_NDK_TOOLCHAIN "${GK_ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake")
set(GK_JAVA "${GK_ANDROID_JAVA_HOME}/bin/java${CMAKE_EXECUTABLE_SUFFIX}")
set(GK_VCPKG_TOOLCHAIN "${GK_REPO_ROOT}/.vcpkg/scripts/buildsystems/vcpkg.cmake")

foreach(requiredPath IN ITEMS
    "${GK_ADB}"
    "${GK_NDK_TOOLCHAIN}"
    "${GK_JAVA}"
    "${GK_VCPKG_TOOLCHAIN}"
    "${GK_ANDROID_SDK_ROOT}/platforms/android-35/android.jar"
)
    if(NOT EXISTS "${requiredPath}")
        message(FATAL_ERROR "Required Android build input not found: ${requiredPath}")
    endif()
endforeach()

execute_process(
    COMMAND "${GK_JAVA}" -version
    OUTPUT_VARIABLE javaVersionOutput
    ERROR_VARIABLE javaVersionError
    RESULT_VARIABLE javaVersionResult
)
set(javaVersionText "${javaVersionOutput}\n${javaVersionError}")
if(NOT javaVersionResult EQUAL 0 OR NOT javaVersionText MATCHES "version \"([0-9]+)")
    message(FATAL_ERROR "Unable to determine the JDK version from ${GK_JAVA}: ${javaVersionText}")
endif()
set(javaMajorVersion "${CMAKE_MATCH_1}")
if(javaMajorVersion LESS 17 OR javaMajorVersion GREATER 23)
    message(FATAL_ERROR
        "Gradle 8.12 requires a supported JDK (17 through 23); found Java ${javaMajorVersion} "
        "at ${GK_ANDROID_JAVA_HOME}. Set GK_ANDROID_JAVA_HOME or JAVA_HOME explicitly.")
endif()

set(slangHints "")
if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
    list(APPEND slangHints "$ENV{VULKAN_SDK}/bin")
endif()
file(GLOB vulkanSdkCandidates LIST_DIRECTORIES true "${GK_REPO_ROOT}/external/VulkanSDK/*")
foreach(candidate IN LISTS vulkanSdkCandidates)
    list(APPEND slangHints "${candidate}/bin" "${candidate}/macOS/bin" "${candidate}/x86_64/bin")
endforeach()
find_program(GK_SLANGC NAMES slangc slangc.exe HINTS ${slangHints} NO_CACHE)
if(NOT GK_SLANGC)
    message(FATAL_ERROR "Host slangc not found. Run 'gnb setup' or set VULKAN_SDK.")
endif()

get_filename_component(cmakeBinDir "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(GK_CMAKE_ROOT "${cmakeBinDir}" DIRECTORY)

set(templateFiles
    build.gradle
    settings.gradle
    gradle.properties
    gradlew
    gradlew.bat
    gradle/wrapper/gradle-wrapper.jar
    gradle/wrapper/gradle-wrapper.properties
    app/build.gradle.in
    app/src/main/AndroidManifest.xml
    app/src/main/java/com/gknext/renderer/GkNextActivity.java
    app/src/main/res/values/strings.xml
    app/src/main/res/values/styles.xml
)
foreach(templateFile IN LISTS templateFiles)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/templates/${templateFile}")
        message(FATAL_ERROR "Android template is incomplete: ${templateFile}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${templateFiles}
    "${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSdlAar.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/ArchiveApk.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidDevice.cmake"
)

file(MAKE_DIRECTORY "${GK_GRADLE_DIR}/app")
file(REMOVE_RECURSE "${GK_GRADLE_DIR}/app/src")
foreach(templatePath IN ITEMS
    build.gradle
    settings.gradle
    gradle.properties
    gradlew
    gradlew.bat
    gradle
)
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/templates/${templatePath}" DESTINATION "${GK_GRADLE_DIR}")
endforeach()
file(COPY "${CMAKE_CURRENT_LIST_DIR}/templates/app/src" DESTINATION "${GK_GRADLE_DIR}/app")

set(GK_SDL_VERSION "3.4.10")
set(GK_SDL_ARCHIVE_SHA256 "f676e29b1b4eb990e24862b6af5f327379dc496c9f5f3e3f48ab75837f95844e")
set(GK_SDL_AAR_SHA256 "e80e8ab1bc969bed28192d34c2029f223ce06cf106aa3d1167220334b581ddb0")
set(GK_SDL_AAR "${GK_CACHE_DIR}/SDL3-${GK_SDL_VERSION}.aar")

foreach(pathVariable IN ITEMS
    GK_REPO_ROOT
    GK_NATIVE_DIR
    GK_ASSET_ROOT
    GK_ANDROID_ASSET_OUTPUT_DIR
    GK_ANDROID_NDK_ROOT
    GK_NDK_TOOLCHAIN
    GK_VCPKG_TOOLCHAIN
    GK_SLANGC
    GK_SDL_AAR
    GK_JNILIBS_DIR
    GK_ANDROID_SIGNING_PROPERTIES
)
    file(TO_CMAKE_PATH "${${pathVariable}}" ${pathVariable})
endforeach()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app/build.gradle.in"
    "${GK_GRADLE_DIR}/app/build.gradle"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app/src/main/AndroidManifest.xml"
    "${GK_GRADLE_DIR}/app/src/main/AndroidManifest.xml"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app/src/main/res/values/strings.xml"
    "${GK_GRADLE_DIR}/app/src/main/res/values/strings.xml"
    @ONLY
)

file(TO_CMAKE_PATH "${GK_ANDROID_SDK_ROOT}" localSdkDir)
file(TO_CMAKE_PATH "${GK_CMAKE_ROOT}" localCmakeDir)
file(WRITE "${GK_GRADLE_DIR}/local.properties"
    "sdk.dir=${localSdkDir}\n"
    "cmake.dir=${localCmakeDir}\n"
)

if(CMAKE_HOST_WIN32)
    set(GK_GRADLE_WRAPPER "${GK_GRADLE_DIR}/gradlew.bat")
else()
    set(GK_GRADLE_WRAPPER "${GK_GRADLE_DIR}/gradlew")
endif()
if(GK_ANDROID_VARIANT STREQUAL "debug")
    set(gradleTask assembleDebug)
    set(gradleApk "${GK_GRADLE_DIR}/app/build/outputs/apk/debug/app-debug.apk")
elseif(GK_ANDROID_VARIANT STREQUAL "relwithdebinfo")
    set(gradleTask assembleRelWithDebInfo)
    set(gradleApk "${GK_GRADLE_DIR}/app/build/outputs/apk/relWithDebInfo/app-relWithDebInfo.apk")
else()
    set(gradleTask assembleRelease)
    if(GK_ANDROID_RELEASE_SIGNED)
        set(gradleApk "${GK_GRADLE_DIR}/app/build/outputs/apk/release/app-release.apk")
    else()
        set(gradleApk "${GK_GRADLE_DIR}/app/build/outputs/apk/release/app-release-unsigned.apk")
    endif()
endif()
set(archivedApk "${GK_APK_DIR}/${GK_ANDROID_APP}-${GK_ANDROID_VARIANT}.apk")

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/android-prepare.stamp"
    COMMAND "${CMAKE_COMMAND}"
        "-DSDL_VERSION=${GK_SDL_VERSION}"
        "-DSDL_ARCHIVE_SHA256=${GK_SDL_ARCHIVE_SHA256}"
        "-DSDL_AAR_SHA256=${GK_SDL_AAR_SHA256}"
        "-DCACHE_DIR=${GK_CACHE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSdlAar.cmake"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/android-prepare.stamp"
    DEPENDS
        "${CMAKE_CURRENT_LIST_FILE}"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSdlAar.cmake"
    COMMENT "Preparing Android Gradle staging and SDL3 AAR"
    VERBATIM
)
add_custom_target(android-prepare DEPENDS "${CMAKE_BINARY_DIR}/android-prepare.stamp")

add_custom_target(android-apk
    COMMAND "${CMAKE_COMMAND}" -E env
        "JAVA_HOME=${GK_ANDROID_JAVA_HOME}"
        "ANDROID_SDK_ROOT=${GK_ANDROID_SDK_ROOT}"
        "ANDROID_NDK_HOME=${GK_ANDROID_NDK_ROOT}"
        "VULKAN_SDK=$ENV{VULKAN_SDK}"
        "${GK_GRADLE_WRAPPER}" "${gradleTask}" --no-daemon --console=plain
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_APK=${gradleApk}"
        "-DOUTPUT_APK=${archivedApk}"
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/ArchiveApk.cmake"
    DEPENDS android-prepare
    WORKING_DIRECTORY "${GK_GRADLE_DIR}"
    COMMENT "Building ${GK_ANDROID_VARIANT} Android APK"
    USES_TERMINAL
    VERBATIM
)

if(GK_ANDROID_VARIANT STREQUAL "debug" OR GK_ANDROID_VARIANT STREQUAL "relwithdebinfo" OR GK_ANDROID_RELEASE_SIGNED)
    add_custom_target(android-install
        COMMAND "${CMAKE_COMMAND}"
            "-DADB=${GK_ADB}"
            "-DSERIAL=${GK_ANDROID_SERIAL}"
            "-DACTION=install"
            "-DAPK=${archivedApk}"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidDevice.cmake"
        DEPENDS android-apk
        USES_TERMINAL
        VERBATIM
    )
endif()

if(GK_ANDROID_VARIANT STREQUAL "debug" OR GK_ANDROID_VARIANT STREQUAL "relwithdebinfo")
    add_custom_target(android-run
        COMMAND "${CMAKE_COMMAND}"
            "-DADB=${GK_ADB}"
            "-DSERIAL=${GK_ANDROID_SERIAL}"
            "-DACTION=run"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidDevice.cmake"
        DEPENDS android-install
        USES_TERMINAL
        VERBATIM
    )
endif()

add_custom_target(android-logcat
    COMMAND "${CMAKE_COMMAND}"
        "-DADB=${GK_ADB}"
        "-DSERIAL=${GK_ANDROID_SERIAL}"
        "-DACTION=logcat"
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidDevice.cmake"
    USES_TERMINAL
    VERBATIM
)

message(STATUS "Android application: ${GK_ANDROID_APP} (${GK_ANDROID_APPLICATION_ID})")
message(STATUS "Android driver variant: ${GK_ANDROID_VARIANT}")
message(STATUS "Android SDK: ${GK_ANDROID_SDK_ROOT}")
message(STATUS "Android NDK: ${GK_ANDROID_NDK_ROOT}")
message(STATUS "Java home: ${GK_ANDROID_JAVA_HOME}")
message(STATUS "CMake used by driver and AGP: ${CMAKE_COMMAND}")
if(GK_ANDROID_VARIANT STREQUAL "release")
    if(GK_ANDROID_RELEASE_SIGNED)
        message(STATUS "Android release signing: ${GK_ANDROID_SIGNING_PROPERTIES}")
    else()
        message(STATUS "Android release signing: disabled (unsigned build verification)")
    endif()
endif()
message(STATUS "Android APK output: ${archivedApk}")
