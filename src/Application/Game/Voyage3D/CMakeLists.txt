file(GLOB_RECURSE voyage3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(Voyage3D ${voyage3DSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(Voyage3D MODULES ${GK_STANDARD_RUNTIME_MODULES})
