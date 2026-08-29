file(GLOB_RECURSE gkNextMinimalRendererSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

# MinimalMain.cpp is this application's whole point: it starts the engine without the shared
# desktop entry point, so nothing but engine core is exercised.
gk_add_application(gkNextMinimalRenderer CORE_ONLY MINIMAL_LINK NO_DEFAULT_MAIN
    SOURCES ${gkNextMinimalRendererSources}
)
