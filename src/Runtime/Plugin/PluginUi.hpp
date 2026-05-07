#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/vec2.hpp>

namespace Runtime::PluginUi
{
    ENGINE_API bool GetMainViewportRect(glm::vec2& outPosition, glm::vec2& outSize);
    ENGINE_API glm::vec2 CalcTextSize(std::string_view text);
    ENGINE_API void AddText(std::string_view text, const glm::vec2& position, float scale, uint32_t color);
    ENGINE_API void AddRectFilled(const glm::vec2& min, const glm::vec2& max, uint32_t color, float rounding);
    ENGINE_API void AddRect(const glm::vec2& min, const glm::vec2& max, uint32_t color, float rounding, float thickness);
}
