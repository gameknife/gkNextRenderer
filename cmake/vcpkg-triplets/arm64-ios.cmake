set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)

# Keep third-party static libraries on the same deployment contract as the app.
# The upstream community triplet otherwise defaults to the active SDK version.
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"
)
