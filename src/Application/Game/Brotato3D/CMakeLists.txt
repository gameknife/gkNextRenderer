file(GLOB_RECURSE brotato3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(Brotato3D
    SOURCES ${brotato3DSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES} ScadLoader
    DEFINES DEV_MODE=1
)
