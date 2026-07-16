#pragma once

namespace Modules::LiveCoding::CppLiveCoding
{
    // Starts the optional Live++ synchronized agent. Safe to call more than once.
    bool Startup();

    // Applies a pending patch before the engine starts its next frame.
    void BeginFrame();

    // Stops the agent. Safe to call when startup failed or was disabled.
    void Shutdown();
}
