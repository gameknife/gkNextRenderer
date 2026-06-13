#pragma once

#include <glm/glm.hpp>

namespace StudioSim::Config
{
    constexpr float kGroundY = 0.15f;
    constexpr float kWalkSpeed = 3.0f;
    constexpr float kNavCellSize = 0.5f;
    constexpr float kAgentRadius = 0.3f;
    constexpr float kSeparationRadius = 0.6f;
    constexpr float kSeparationStrength = 1.2f;

    constexpr bool kUseScadRigVisual = true;
    inline constexpr const char* kAgentRigPath = "assets/scad/characters/agent_basic.scad";
    constexpr glm::vec3 kParkedPosition{0.0f, -100.0f, 0.0f};
}
