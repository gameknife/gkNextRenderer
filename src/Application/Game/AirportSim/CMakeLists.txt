file(GLOB_RECURSE airportSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(AirportSim ${airportSimSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(AirportSim MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(AirportSim PRIVATE NextAI NextGameplay)
