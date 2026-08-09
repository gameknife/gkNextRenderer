file(GLOB_RECURSE characterDemoSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(CharacterDemo ${characterDemoSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(CharacterDemo MODULES ${GK_STANDARD_RUNTIME_MODULES})
target_link_libraries(CharacterDemo PRIVATE NextGameplay)
