file(GLOB_RECURSE dotNetSandboxSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(DotNetSandbox
    SOURCES ${dotNetSandboxSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextDotNet
)

gk_dotnet_managed_game(DotNetSandbox
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/GkNext.Game/GkNext.Game.csproj"
    DIR game
)
