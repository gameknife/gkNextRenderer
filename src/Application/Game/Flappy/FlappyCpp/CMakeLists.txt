file(GLOB_RECURSE flappyCppSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(FlappyCpp
    ${flappyCppSources}
    ${GK_FLAPPY_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(FlappyCpp MODULES ${GK_STANDARD_RUNTIME_MODULES})
target_include_directories(FlappyCpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
