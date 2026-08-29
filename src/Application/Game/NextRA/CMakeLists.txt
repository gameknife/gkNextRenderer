file(GLOB_RECURSE nextRaSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(NextRA
    SOURCES ${nextRaSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES}
    DEFINES DEV_MODE=1
)
