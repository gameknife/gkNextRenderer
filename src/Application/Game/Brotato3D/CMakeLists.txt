file(GLOB_RECURSE brotato3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(Brotato3D ${brotato3DSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(Brotato3D MODULES ${GK_STANDARD_RUNTIME_MODULES} ScadLoader)
target_compile_definitions(Brotato3D PRIVATE DEV_MODE=1)
