file(GLOB_RECURSE airportSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(AirportSim
    SOURCES ${airportSimSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextAI
    LINK NextGameplay
)
