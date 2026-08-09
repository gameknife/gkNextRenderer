file(GLOB_RECURSE nextTotalwarSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(NextTotalwar ${nextTotalwarSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(NextTotalwar MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(NextTotalwar PRIVATE NextGameplay)
