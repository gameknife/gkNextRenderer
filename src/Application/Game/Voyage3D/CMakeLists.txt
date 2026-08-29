file(GLOB_RECURSE voyage3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(Voyage3D
    SOURCES ${voyage3DSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES}
)
