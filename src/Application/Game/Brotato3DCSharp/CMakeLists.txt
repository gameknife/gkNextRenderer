file(GLOB_RECURSE brotato3DCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(Brotato3DCSharp ${brotato3DCSharpSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(Brotato3DCSharp MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
    NextDotNet
)

gk_dotnet_managed_game(Brotato3DCSharp
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/Brotato3D/Brotato3DCSharp/Brotato3DCSharp.csproj"
    DIR brotato3d
)
