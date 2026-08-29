file(GLOB_RECURSE brickPlayerSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(BrickPlayer
    SOURCES ${brickPlayerSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        LDrawLoader
)
