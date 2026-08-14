file(GLOB_RECURSE flappyCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(FlappyCSharp
    ${flappyCSharpSources}
    ${GK_FLAPPY_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(FlappyCSharp MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    NextDotNet
)
target_include_directories(FlappyCSharp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

gk_dotnet_managed_game(FlappyCSharp
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/Flappy/FlappyCSharp/FlappyCSharp.csproj"
    DIR flappy
)
