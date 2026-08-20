#pragma once

// Optional graphics-debugger integration owned by DevTools.

namespace Runtime::RenderDoc
{
    // Returns true when the build was configured with the locally installed RenderDoc API.
    bool IsSupported();

    // Loads the RenderDoc application API before Vulkan is created.
    bool Initialize();

    // Captures the next presented frame. Poll() opens the resulting capture in the replay UI.
    bool RequestCapture();

    // Checks for a newly completed capture and opens it in RenderDoc.
    void Poll();
}
