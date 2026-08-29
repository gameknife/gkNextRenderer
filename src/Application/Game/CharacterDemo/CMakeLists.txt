file(GLOB_RECURSE characterDemoSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(CharacterDemo
    SOURCES ${characterDemoSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES}
    LINK NextGameplay
)
