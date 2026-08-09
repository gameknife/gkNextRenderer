#pragma once

#include <imgui.h>

namespace NextUI::Scaling
{
    struct FViewportRect
    {
        ImVec2 Position;
        ImVec2 Size;
    };

    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth = 1280.0f, float baseHeight = 720.0f);

    // Vulkan viewport rectangles use framebuffer pixels, while ImGui and ImGuizmo use
    // logical screen coordinates. Keep the conversion in one place so high-DPI
    // applications do not accidentally mix the two coordinate spaces.
    FViewportRect MainFramebufferToImGuiViewport(const ImVec2& position, const ImVec2& size);
    FViewportRect ImGuiToMainFramebufferViewport(const ImVec2& position, const ImVec2& size);
}
