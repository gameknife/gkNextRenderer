file(GLOB_RECURSE citySolSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(CitySolSim
    SOURCES ${citySolSimSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
