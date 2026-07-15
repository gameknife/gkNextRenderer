file(GLOB_RECURSE brickPlayerSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(BrickPlayer ${brickPlayerSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(BrickPlayer MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
)
