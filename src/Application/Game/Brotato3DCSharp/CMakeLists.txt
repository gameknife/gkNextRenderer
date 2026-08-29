file(GLOB_RECURSE brotato3DCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(Brotato3DCSharp
    SOURCES ${brotato3DCSharpSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
)

gk_dotnet_managed_game(Brotato3DCSharp
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/Brotato3D/Brotato3DCSharp/Brotato3DCSharp.csproj"
    DIR brotato3d
)
