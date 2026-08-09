#pragma once

#include <functional>

class NextEngine;

namespace Runtime::DevToolsUI
{
    void DrawDeveloperStatusBar(NextEngine& engine,
                                const char* windowId = "AppBottomBar",
                                float height = 30.0f,
                                std::function<void()> onMemoryClicked = {},
                                bool memoryActive = false,
                                std::function<void()> onCppReloadClicked = {},
                                bool cppLiveCodingAvailable = false);
}
