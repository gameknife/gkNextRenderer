#include "Application/Game/NextAstrobot/World/HazardSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace NextAstrobot
{
    namespace
    {
        // Distance from a point to a segment, used for the laser beam.
        float DistanceToSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 ab = b - a;
            const float lengthSq = glm::dot(ab, ab);
            if (lengthSq < 1e-6f)
            {
                return glm::length(point - a);
            }
            const float t = std::clamp(glm::dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
            return glm::length(point - (a + ab * t));
        }
    }

    void FHazardSystem::Unbind()
    {
        hazards_.clear();
        killPlaneY_ = -1000.0f;
    }

    void FHazardSystem::Bind(const FLevelIndex& index, float killPlaneY)
    {
        Unbind();
        killPlaneY_ = killPlaneY;

        for (const FTypedNode& entry : index.hazards)
        {
            FHazard hazard;
            hazard.kind = static_cast<EHazardKind>(entry.kind);
            hazard.worldPos = entry.node.worldPos;
            hazard.worldRot = entry.node.worldRot;
            switch (hazard.kind)
            {
            case EHazardKind::Lava:
            {
                const float length = static_cast<float>(entry.node.Number("L", 8.0));
                const float depth = static_cast<float>(entry.node.Number("D", 6.0));
                // The crust is inset from the rim; standing on the rim itself is safe.
                hazard.extent = glm::vec3((length - 0.9f) * 0.5f, 0.0f, (depth - 0.9f) * 0.5f);
                hazard.surfaceHeight = 0.5f;
                break;
            }
            case EHazardKind::Water:
            {
                const float length = static_cast<float>(entry.node.Number("L", 8.0));
                const float depth = static_cast<float>(entry.node.Number("D", 6.0));
                const float poolDepth = static_cast<float>(entry.node.Number("depth", 0.6));
                hazard.extent = glm::vec3((length - 0.8f) * 0.5f, 0.0f, (depth - 0.8f) * 0.5f);
                hazard.surfaceHeight = poolDepth;
                break;
            }
            case EHazardKind::Spikes:
            {
                const float length = static_cast<float>(entry.node.Number("len", 3.0));
                const float width = static_cast<float>(entry.node.Number("w", 1.0));
                hazard.extent = glm::vec3(length * 0.5f, 0.0f, width * 0.5f);
                hazard.surfaceHeight = 0.6f;
                break;
            }
            case EHazardKind::Laser:
            {
                hazard.extent = glm::vec3(static_cast<float>(entry.node.Number("L", 6.0)), 0.0f, 0.0f);
                hazard.radius = 0.45f;
                hazard.surfaceHeight = 0.7f;
                break;
            }
            case EHazardKind::SpikeBall:
            {
                const float ballRadius = static_cast<float>(entry.node.Number("r", 0.7));
                hazard.radius = ballRadius + 0.45f;
                hazard.surfaceHeight = ballRadius + 0.35f;
                break;
            }
            }
            hazards_.push_back(hazard);
        }
    }

    std::string FHazardSystem::Check(const glm::vec3& playerFoot, float playerHeight) const
    {
        if (playerFoot.y < killPlaneY_)
        {
            return "fall";
        }

        const glm::vec3 centre = playerFoot + glm::vec3(0.0f, playerHeight * 0.5f, 0.0f);
        for (const FHazard& hazard : hazards_)
        {
            const glm::vec3 local = glm::inverse(hazard.worldRot) * (playerFoot - hazard.worldPos);
            switch (hazard.kind)
            {
            case EHazardKind::Lava:
                if (std::abs(local.x) <= hazard.extent.x && std::abs(local.z) <= hazard.extent.z &&
                    local.y < hazard.surfaceHeight + 0.15f)
                {
                    return "lava";
                }
                break;
            case EHazardKind::Water:
                // The water surface is part of the pool's collision mesh, so a player who
                // fell in is standing exactly on it rather than sinking below it.
                if (std::abs(local.x) <= hazard.extent.x && std::abs(local.z) <= hazard.extent.z &&
                    local.y < hazard.surfaceHeight + 0.15f)
                {
                    return "water";
                }
                break;
            case EHazardKind::Spikes:
                if (std::abs(local.x) <= hazard.extent.x && std::abs(local.z) <= hazard.extent.z &&
                    local.y < hazard.surfaceHeight)
                {
                    return "spikes";
                }
                break;
            case EHazardKind::Laser:
            {
                // The beam runs from the emitter to the receiver at SCAD z = 0.7.
                const glm::vec3 from = hazard.worldPos + hazard.worldRot * glm::vec3(0.3f, hazard.surfaceHeight, 0.0f);
                const glm::vec3 to =
                    hazard.worldPos + hazard.worldRot * glm::vec3(hazard.extent.x - 0.3f, hazard.surfaceHeight, 0.0f);
                if (DistanceToSegment(centre, from, to) < hazard.radius + playerHeight * 0.25f)
                {
                    return "laser";
                }
                break;
            }
            case EHazardKind::SpikeBall:
            {
                const glm::vec3 ball = hazard.worldPos + hazard.worldRot * glm::vec3(0.0f, hazard.surfaceHeight, 0.0f);
                if (glm::length(centre - ball) < hazard.radius + playerHeight * 0.25f)
                {
                    return "spike_ball";
                }
                break;
            }
            }
        }
        return {};
    }
}
