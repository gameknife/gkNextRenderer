#pragma once

namespace Modules::Scad
{
    // Registers the OpenSCAD (.scad) scene loader into Assets::FLoaderRegistry.
    // Call once from the application entry before loading/scanning scenes.
    void Register();
}
