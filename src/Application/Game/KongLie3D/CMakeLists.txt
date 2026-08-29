file(GLOB_RECURSE kongLie3DSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(KongLie3D
    SOURCES ${kongLie3DSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES}
)
