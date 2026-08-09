file(GLOB_RECURSE citySolSimSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(CitySolSim ${citySolSimSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(CitySolSim MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(CitySolSim PRIVATE NextGameplay)
