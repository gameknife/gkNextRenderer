#pragma once

namespace AppCommon
{
    // Registers the showcase ".proc" demo scenes (CornellBox, RTIO, GIBootcamp,
    // Material/Lighting/Camera/Animation/PhysicsShowcase) with the loader
    // registry. Call once from the application entry before scene scanning.
    void RegisterDemoScenes();
}
