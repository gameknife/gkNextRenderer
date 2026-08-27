file(GLOB_RECURSE gkNextLauncherSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(gkNextLauncher
    ${gkNextLauncherSources}
    ${GK_DESKTOP_MAIN_SOURCE}
)

# The union of what the games in assets/configs/games need. Native modules are static libraries
# picked at link time, so a launcher can only run a game whose modules it already carries; the
# manifest's requiredModules list is checked against this set at startup.
gk_configure_application(gkNextLauncher MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    NextDotNet
    ScadLoader
    LDrawLoader
    SplatLoader
)

# Every managed game this launcher can start has to be published before it is worth running. The
# launcher itself brings no C# of its own: it loads assemblies chosen at runtime, which is exactly
# what gk_dotnet_stub_game exists to say.
gk_dotnet_stub_game(gkNextLauncher)
foreach(managedGame Brotato3DCSharpManagedGame FlappyCSharpManagedGame DotNetSandboxManagedGame)
    if(TARGET ${managedGame})
        add_dependencies(gkNextLauncher ${managedGame})
    endif()
endforeach()
