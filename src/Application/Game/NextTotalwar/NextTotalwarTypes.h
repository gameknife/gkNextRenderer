#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace NextTotalwar
{
    enum class EUnitType : uint8_t
    {
        Spearman,
        Swordsman,
        Archer,
    };

    enum class ERegimentState : uint8_t
    {
        Idle,
        Marching,
        Reforming,
    };

    struct FUnitDef
    {
        EUnitType type = EUnitType::Spearman;
        const char* id = "spearman";
        const char* displayName = "Spearmen";
        float marchSpeed = 8.0f;
        float catchUpFactor = 1.45f;
        int defaultRanks = 4;
        float fileSpacing = 1.15f;
        float rankSpacing = 1.35f;
    };

    struct FSoldier
    {
        glm::vec3 position{};
        float yaw = 0.0f;
        int slotIndex = -1;
        float phaseOffset = 0.0f;
        std::shared_ptr<Assets::Node> worldNode;
        Assets::Node* renderNode = nullptr;
    };

    struct FRegiment
    {
        int id = -1;
        int faction = 0;
        const FUnitDef* def = nullptr;
        glm::vec3 anchor{};
        float facing = 0.0f;
        int ranks = 4;
        bool selected = false;
        ERegimentState state = ERegimentState::Idle;
        glm::vec3 orderTarget{};
        float orderFacing = 0.0f;
        std::vector<glm::vec3> path;
        size_t pathCursor = 0;
        std::vector<FSoldier> soldiers;
    };
}
