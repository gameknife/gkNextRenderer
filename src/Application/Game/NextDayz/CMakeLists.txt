file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(NextDayz
    SOURCES ${nextDayzSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
