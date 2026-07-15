file(GLOB_RECURSE studioSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(StudioSim ${studioSimSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(StudioSim MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(StudioSim PRIVATE NextAI NextGameplay)
