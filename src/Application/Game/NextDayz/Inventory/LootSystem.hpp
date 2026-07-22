#pragma once

// ============================================================================
// LootSystem.hpp - Derives pickups directly from the map's scene nodes. On scene
// load it scans scene.Nodes() for names in the map-item -> game-item table and
// registers one entry per matching node (so loot always follows what the artist
// placed). Each frame it finds the entry the player is near and aiming at; E
// grants its items to the Inventory and hides the source node.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"

class NextEngine;

namespace Assets
{
    class Scene;
    class Node;
}

namespace NextDayz
{
    struct FLootGrant
    {
        std::string id;
        std::string displayName;
        EItemKind kind;
        int count;
    };

    struct FLootDef
    {
        std::string displayName;          // shown in the interaction prompt
        std::vector<FLootGrant> grants;
    };

    struct FLootEntry
    {
        uint32_t nodeInstanceId = 0;
        glm::vec3 worldPos{0.0f};
        const FLootDef* def = nullptr;
        bool looted = false;
    };

    class LootSystem
    {
    public:
        void Configure(const FLootConfig& config) { config_ = config; }

        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();

        // Recomputes the hovered entry from player position + view direction.
        void Update(const glm::vec3& eyePosition, const glm::vec3& viewForward);

        // Nearest registered loot to an approximate XZ (used to derive a safe
        // spawn on real ground next to supplies). Returns false if no loot.
        bool NearestLootPos(const glm::vec2& approxXZ, glm::vec3& outWorldPos) const;

        bool HasHovered() const { return hoveredIndex_ >= 0; }
        const FLootEntry* Hovered() const;
        std::string HoveredPrompt() const; // "AK-74" (empty if none)

        // Grants the hovered entry to the inventory and hides its node.
        // Returns the def that was picked up (nullptr if nothing hovered).
        const FLootDef* PickupHovered(Inventory& inventory, NextEngine& engine);

        int RemainingCount() const;

    private:
        FLootConfig config_{};
        std::vector<FLootEntry> entries_;
        int hoveredIndex_ = -1;
    };
}
