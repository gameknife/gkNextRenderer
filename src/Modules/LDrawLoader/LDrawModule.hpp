#pragma once

namespace Modules::LDraw
{
    // Registers the LDraw (.ldr/.mpd) scene loader into Assets::FLoaderRegistry.
    // Call once from the application entry before loading/scanning scenes.
    void Register();
}
