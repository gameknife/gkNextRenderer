#pragma once

#include "AnchorMap.h"

#include <glm/glm.hpp>

#include <span>
#include <string_view>

namespace NextGameplay::Sim
{
    struct FAnchorDebugPoint
    {
        std::string_view name;
        std::string_view category;
        glm::vec3 worldPos{0.0f};
        bool enabled = true;
    };

    struct FAnchorDebugDrawConfig
    {
        float labelHeight = 1.4f;
        float markerRadius = 7.0f;
        bool showCoordinates = true;
        bool showDisabledState = true;
    };

    void DrawAnchorDebugOverlay(const glm::mat4& viewProjection, std::span<const FAnchorPoi> points,
                                const FAnchorDebugDrawConfig& config = {});
    void DrawAnchorDebugOverlay(const glm::mat4& viewProjection, std::span<const FAnchorDebugPoint> points,
                                const FAnchorDebugDrawConfig& config = {});
}
