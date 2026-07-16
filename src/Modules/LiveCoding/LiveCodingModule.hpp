#pragma once

class NextEngine;

namespace Modules::LiveCoding
{
    // Installs shader hot reload and, when compiled in, starts the Live++ agent.
    // Call from the application entry before NextEngine::Start.
    void Install(NextEngine& engine, bool enableCppLiveCoding = true);

    // Applies a pending C++ patch at the outer frame boundary.
    void BeginFrame();

    // Requests a Live++ compile/reload pass. Returns false when Live++ is unavailable.
    bool RequestCppReload();

    // Returns whether the Live++ agent is available to the current process.
    bool IsCppLiveCodingAvailable();

    // Stops the C++ live coding agent before engine teardown.
    void Shutdown();
}
