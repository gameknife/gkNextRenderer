#pragma once

namespace Modules::Gltf
{
    // Registers the glTF/GLB scene loader into Assets::FLoaderRegistry.
    // Call from the application entry before any scene loads.
    void Register();
}
