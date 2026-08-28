file(GLOB_RECURSE flappyCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

# No GK_FLAPPY_COMMON_SOURCES here: FlappyConfig.cpp is the C++ game's config reader, and the C#
# game reads the same JSON through its own FlappyConfigLoader. Sharing the native one would give
# the parity pair a shared implementation exactly where it is supposed to be independent.
if(ANDROID)
    # The APK's initial scene comes from flappy.game.json once the managed game is running; this
    # only keeps the engine from loading a renderer showcase in the seconds before that.
    gk_add_android_application(FlappyCSharp
        SOURCES ${flappyCSharpSources}
        MODULES ${GK_STANDARD_RUNTIME_MODULES} NextDotNet
        SCENE "Empty.proc"
    )
else()
    add_executable(FlappyCSharp
        ${flappyCSharpSources}
        ${GK_DESKTOP_MAIN_SOURCE}
    )
    gk_configure_application(FlappyCSharp MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextDotNet
    )
endif()

gk_dotnet_managed_game(FlappyCSharp
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/Flappy/FlappyCSharp/FlappyCSharp.csproj"
    DIR flappy
)
