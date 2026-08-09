file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(NextDayz ${nextDayzSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(NextDayz MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(NextDayz PRIVATE NextGameplay)
