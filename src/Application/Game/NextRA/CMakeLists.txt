file(GLOB_RECURSE nextRaSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(NextRA ${nextRaSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(NextRA MODULES ${GK_STANDARD_RUNTIME_MODULES})
target_compile_definitions(NextRA PRIVATE DEV_MODE=1)
