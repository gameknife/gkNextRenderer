file(GLOB_RECURSE dotNetSandboxSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(DotNetSandbox
    ${dotNetSandboxSources}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(DotNetSandbox MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    NextDotNet
)

gk_dotnet_managed_game(DotNetSandbox
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/GkNext.Game/GkNext.Game.csproj"
    DIR game
)
