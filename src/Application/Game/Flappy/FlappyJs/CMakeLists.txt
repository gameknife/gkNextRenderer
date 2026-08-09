file(GLOB_RECURSE flappyJsSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(FlappyJs ${flappyJsSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(FlappyJs MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    NextQuickJS
)
target_include_directories(FlappyJs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
