file(GLOB_RECURSE scadLibrarySources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

# Viewport picking uses Scene's CPU acceleration structure. ScadLibrary does
# not simulate rigid bodies, so avoid installing Jolt and cooking collision
# meshes on every iterative scene rebuild.
set(scadLibraryRuntimeModules ${GK_STANDARD_RUNTIME_MODULES})
list(REMOVE_ITEM scadLibraryRuntimeModules NextPhysics)

gk_add_application(ScadLibrary
    SOURCES ${scadLibrarySources}
    MODULES
        ${scadLibraryRuntimeModules}
        ScadLoader
        NextAI
    LINK NextGameplay
)
