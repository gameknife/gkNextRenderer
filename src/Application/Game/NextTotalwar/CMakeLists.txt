file(GLOB_RECURSE nextTotalwarSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(NextTotalwar
    SOURCES ${nextTotalwarSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
