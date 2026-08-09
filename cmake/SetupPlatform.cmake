set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# Apple presets currently target arm64 on both macOS and iOS.
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    if(IOS AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=iphoneos*] "arm64")
    endif()
endif()

option(IOS_SKIP_CODE_SIGN "Disable iOS code signing (useful for CI xcodebuild)" OFF)

# Signing identity for iOS device builds. Left empty on purpose: these values are
# per-developer and must not live in the repository. Set them at configure time when
# IOS_SKIP_CODE_SIGN=OFF.
set(IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple Developer Team ID used to sign iOS builds")
set(IOS_CODE_SIGN_IDENTITY "Apple Development" CACHE STRING "Xcode code signing identity for iOS builds")
set(IOS_PROVISIONING_PROFILE "" CACHE STRING "Provisioning profile specifier for iOS builds")

foreach (OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_BINARY_DIR}/lib)
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
