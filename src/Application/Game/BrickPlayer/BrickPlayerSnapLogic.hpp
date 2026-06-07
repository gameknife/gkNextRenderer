#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "BrickPlayerLDrawShadow.hpp"

namespace BrickPlayer::Snap
{
    enum class EConnectorProfileClass : uint8_t
    {
        Unknown,
        Round,
        Axle,
    };

    struct FScaleMetrics
    {
        float lduToWorldScale = 0.0f;
        float studPitch = 0.0f;
        float plateHeight = 0.0f;
    };

    struct FHoverFilterResult
    {
        glm::vec3 hoverNormal{0.0f, 1.0f, 0.0f};
        float hoverDistanceLimit = 0.0f;
        float hoverDepthLimit = 0.0f;
        float hoverDepth = 0.0f;
        float hoverDistance = 0.0f;
        bool passes = false;
    };

    FScaleMetrics BuildScaleMetrics(float lduToWorldScale);
    glm::vec3 NormalizeOrDefault(const glm::vec3& value, const glm::vec3& fallback);
    EConnectorProfileClass ClassifyConnectorProfile(const Shadow::FSnapConnector& connector);
    bool AreConnectorsCompatible(const Shadow::FSnapConnector& dragged,
                                 const Shadow::FSnapConnector& target,
                                 float lduToWorldScale);
    FHoverFilterResult EvaluateHoverFilter(const Shadow::FSnapConnector& target,
                                           const glm::vec3& targetWorldPosition,
                                           const glm::vec3& targetWorldAxis,
                                           const glm::vec3& hoverPoint,
                                           const glm::vec3& hoverNormal,
                                           float lduToWorldScale);
}
