file(GLOB_RECURSE nextWorldTravelSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(NextWorldTravel
    SOURCES ${nextWorldTravelSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
