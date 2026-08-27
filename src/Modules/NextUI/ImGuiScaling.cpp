#include "Modules/NextUI/ImGuiScaling.hpp"

#include <algorithm>

namespace NextUI::Scaling
{
    namespace
    {
        ImVec2 GetFramebufferScale()
        {
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            return ImVec2(scale.x > 0.0f ? scale.x : 1.0f,
                          scale.y > 0.0f ? scale.y : 1.0f);
        }
    }

    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth, float baseHeight)
    {
        if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f || baseWidth <= 0.0f || baseHeight <= 0.0f)
        {
            return 1.0f;
        }

        return std::max(0.75f, std::min(viewport->Size.x / baseWidth, viewport->Size.y / baseHeight));
    }

    FViewportRect MainFramebufferToImGuiViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return {
            .Position = ImVec2(origin.x + position.x / scale.x, origin.y + position.y / scale.y),
            .Size = ImVec2(size.x / scale.x, size.y / scale.y),
        };
    }

    FViewportRect ImGuiToMainFramebufferViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return {
            .Position = ImVec2((position.x - origin.x) * scale.x, (position.y - origin.y) * scale.y),
            .Size = ImVec2(size.x * scale.x, size.y * scale.y),
        };
    }
}
