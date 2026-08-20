#pragma once

// Dispatch controller for the optional ambient-bake implementation.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vulkan::AmbientBake
{
    inline constexpr uint32_t minGroupsPerFrame = 1u;
    inline constexpr uint32_t maxGroupsPerFrame = 1u << 20u;

    // Plan the next dispatch size from wall-clock frame time. The frame time includes the
    // previous bake dispatch, so this proportional controller converges toward the requested
    // total frame rate without requiring a device timestamp query.
    inline uint32_t PlanNextDispatchGroups(uint32_t currentGroups, double frameSeconds, uint32_t targetFps)
    {
        const uint32_t current = std::clamp(currentGroups, minGroupsPerFrame, maxGroupsPerFrame);
        if (!std::isfinite(frameSeconds) || frameSeconds <= 0.0 || targetFps == 0u)
        {
            return current;
        }

        const double targetFrameSeconds = 1.0 / static_cast<double>(targetFps);
        const double frameRatio = targetFrameSeconds / std::max(frameSeconds, 1.0e-3);

        // Square-root the correction to keep noisy frame-time samples from making the batch
        // oscillate, while still allowing the controller to catch up quickly after a revision.
        const double correction = std::sqrt(std::clamp(frameRatio, 0.25, 4.0));
        const double desired = static_cast<double>(current) * correction;
        const uint32_t proposed = frameRatio >= 1.0
            ? static_cast<uint32_t>(std::ceil(desired))
            : static_cast<uint32_t>(std::floor(desired));

        const uint32_t lower = std::max(minGroupsPerFrame, current / 2u);
        const uint32_t upper = current > maxGroupsPerFrame / 2u
            ? maxGroupsPerFrame
            : std::max(minGroupsPerFrame, current * 2u);
        return std::clamp(proposed, lower, upper);
    }
}
