#include "BrickPlayerSnapLogic.hpp"

namespace BrickPlayer::Snap
{

    FScaleMetrics BuildScaleMetrics(float lduToWorldScale)
    {
        return {
            lduToWorldScale,
            20.0f * lduToWorldScale,
            8.0f * lduToWorldScale,
        };
    }

    glm::vec3 NormalizeOrDefault(const glm::vec3& value, const glm::vec3& fallback)
    {
        if (glm::dot(value, value) > 1e-6f)
        {
            return glm::normalize(value);
        }

        if (glm::dot(fallback, fallback) > 1e-6f)
        {
            return glm::normalize(fallback);
        }

        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    EConnectorProfileClass ClassifyConnectorProfile(const Shadow::FSnapConnector& connector)
    {
        for (const Shadow::FCylinderSection& section : connector.sections)
        {
            if (section.profile.empty())
            {
                continue;
            }

            const char profileChar = static_cast<char>(std::toupper(section.profile.front()));
            if (profileChar == 'A')
            {
                return EConnectorProfileClass::Axle;
            }
            if (profileChar == 'R' || profileChar == 'S' || profileChar == '_')
            {
                return EConnectorProfileClass::Round;
            }
        }

        return EConnectorProfileClass::Unknown;
    }

    bool AreConnectorsCompatible(const Shadow::FSnapConnector& dragged,
                                 const Shadow::FSnapConnector& target,
                                 float lduToWorldScale)
    {
        if (dragged.kind != Shadow::ESnapKind::Cylinder
            || target.kind != Shadow::ESnapKind::Cylinder)
        {
            return false;
        }

        if (dragged.gender == Shadow::ESnapGender::Unknown
            || target.gender == Shadow::ESnapGender::Unknown
            || dragged.gender == target.gender)
        {
            return false;
        }

        const std::string draggedGroup = ToLowerCopy(dragged.group);
        const std::string targetGroup = ToLowerCopy(target.group);
        if (!draggedGroup.empty() && !targetGroup.empty() && draggedGroup != targetGroup)
        {
            return false;
        }

        const EConnectorProfileClass draggedProfile = ClassifyConnectorProfile(dragged);
        const EConnectorProfileClass targetProfile = ClassifyConnectorProfile(target);
        if (draggedProfile != EConnectorProfileClass::Unknown
            && targetProfile != EConnectorProfileClass::Unknown
            && draggedProfile != targetProfile)
        {
            return false;
        }

        const FScaleMetrics scaleMetrics = BuildScaleMetrics(lduToWorldScale);
        const float radiusTolerance = std::max(scaleMetrics.studPitch * 0.15f, 0.002f);
        const float lengthTolerance = std::max(scaleMetrics.studPitch * 0.2f, 0.004f);
        if (glm::abs(dragged.radius - target.radius) > radiusTolerance)
        {
            return false;
        }

        const Shadow::FSnapConnector& male =
            dragged.gender == Shadow::ESnapGender::Male ? dragged : target;
        const Shadow::FSnapConnector& female =
            dragged.gender == Shadow::ESnapGender::Female ? dragged : target;

        if (male.length > female.length + lengthTolerance && !female.slide)
        {
            return false;
        }

        return true;
    }

    FHoverFilterResult EvaluateHoverFilter(const Shadow::FSnapConnector& target,
                                           const glm::vec3& targetWorldPosition,
                                           const glm::vec3& targetWorldAxis,
                                           const glm::vec3& hoverPoint,
                                           const glm::vec3& hoverNormal,
                                           float lduToWorldScale)
    {
        FHoverFilterResult result;
        result.hoverNormal = NormalizeOrDefault(hoverNormal, glm::vec3(0.0f, 1.0f, 0.0f));

        const FScaleMetrics scaleMetrics = BuildScaleMetrics(lduToWorldScale);
        result.hoverDistanceLimit = scaleMetrics.studPitch * 0.75f;
        result.hoverDepthLimit = std::max(target.length * 1.25f, scaleMetrics.studPitch * 0.3f);

        const glm::vec3 hoverToConnector = targetWorldPosition - hoverPoint;
        result.hoverDepth = glm::dot(hoverToConnector, result.hoverNormal);
        const glm::vec3 planarHoverOffset = hoverToConnector - result.hoverNormal * result.hoverDepth;
        result.hoverDistance = glm::length(planarHoverOffset);

        const glm::vec3 normalizedTargetAxis = NormalizeOrDefault(targetWorldAxis, glm::vec3(0.0f, 1.0f, 0.0f));
        result.passes = result.hoverDepth >= -result.hoverDepthLimit
            && result.hoverDistance <= std::max(result.hoverDistanceLimit, target.radius * 2.0f)
            && glm::dot(normalizedTargetAxis, result.hoverNormal) >= 0.2f;
        return result;
    }
}
