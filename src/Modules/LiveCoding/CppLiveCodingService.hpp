#pragma once

namespace Modules::LiveCoding::CppLiveCoding
{
    // Starts the optional Live++ synchronized agent. Safe to call more than once.
    bool Startup();

    // Applies a pending patch before the engine starts its next frame.
    void BeginFrame();

    // Requests compilation and reload of modified C++ files through Live++.
    bool RequestReload();

    // Returns whether the Live++ agent is running.
    bool IsStarted();

    // Stops the agent. Safe to call when startup failed or was disabled.
    void Shutdown();
}
