#include "Common/CoreMinimal.hpp"

#include "Runtime/Plugin/PluginUi.hpp"

#include <imgui.h>

namespace Runtime::PluginUi
{
    bool GetMainViewportRect(glm::vec2& outPosition, glm::vec2& outSize)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return false;
        }

        outPosition = glm::vec2(viewport->Pos.x, viewport->Pos.y);
        outSize = glm::vec2(viewport->Size.x, viewport->Size.y);
        return true;
    }

    glm::vec2 CalcTextSize(std::string_view text)
    {
        const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        return glm::vec2(size.x, size.y);
    }

    void AddText(std::string_view text, const glm::vec2& position, float scale, uint32_t color)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        drawList->AddText(nullptr,
                          ImGui::GetFontSize() * scale,
                          ImVec2(position.x, position.y),
                          color,
                          text.data(),
                          text.data() + text.size());
    }

    void AddRectFilled(const glm::vec2& min, const glm::vec2& max, uint32_t color, float rounding)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        drawList->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), color, rounding);
    }

    void AddRect(const glm::vec2& min, const glm::vec2& max, uint32_t color, float rounding, float thickness)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        drawList->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), color, rounding, 0, thickness);
    }
}
