file(GLOB_RECURSE kongLie3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(KongLie3D ${kongLie3DSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(KongLie3D MODULES ${GK_STANDARD_RUNTIME_MODULES})
