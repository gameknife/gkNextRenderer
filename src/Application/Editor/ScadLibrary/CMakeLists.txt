file(GLOB_RECURSE scadLibrarySources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

# Reuse the editor's offscreen asset-preview provider for SCAD kit thumbnails.
# ScadLibrary already links ScadLoader, so the shared renderer can evaluate
# generated one-module preview sources without changing the kit authoring path.
list(APPEND scadLibrarySources
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/Preview/AssetThumbnailRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/Preview/AssetThumbnailRenderer.hpp"
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
