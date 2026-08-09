file(GLOB_RECURSE scadLibrarySources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(ScadLibrary
    ${scadLibrarySources}
    ${GK_DESKTOP_MAIN_SOURCE}
)

# Viewport picking uses Scene's CPU acceleration structure. ScadLibrary does
# not simulate rigid bodies, so avoid installing Jolt and cooking collision
# meshes on every iterative scene rebuild.
set(scadLibraryRuntimeModules ${GK_STANDARD_RUNTIME_MODULES})
list(REMOVE_ITEM scadLibraryRuntimeModules NextPhysics)

gk_configure_application(ScadLibrary MODULES
    ${scadLibraryRuntimeModules}
    ScadLoader
)
target_link_libraries(ScadLibrary PRIVATE NextGameplay NextAI)
