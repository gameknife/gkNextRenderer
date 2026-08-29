file(GLOB_RECURSE flappyCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(FlappyCSharp
    SOURCES ${flappyCSharpSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextDotNet
)

gk_dotnet_managed_game(FlappyCSharp
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/Flappy/FlappyCSharp/FlappyCSharp.csproj"
    DIR flappy
)
