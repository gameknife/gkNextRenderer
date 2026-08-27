file(GLOB_RECURSE nextWorldTravelSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(NextWorldTravel ${nextWorldTravelSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(NextWorldTravel MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(NextWorldTravel PRIVATE NextGameplay)
