file(GLOB_RECURSE studioSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(StudioSim
    SOURCES ${studioSimSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextAI
    LINK NextGameplay
)
