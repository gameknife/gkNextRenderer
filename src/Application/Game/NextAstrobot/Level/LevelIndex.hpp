#pragma once

// ============================================================================
// LevelIndex.hpp - The .scad level IS the level data. One pass over
// scene.Nodes() buckets every kit_astro module call by name, resolves its
// movable ab_part_* child and parses the module's named parameters out of
// Node::metadata. Everything downstream (mechanisms, collectibles, hazards,
// enemies, interactables) reads this table and drives the scene nodes directly;
// there is no second copy of the level in JSON.
// ============================================================================

#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Modules/ScadLoader/FScadShared.h"

namespace Assets
{
    class Node;
    class Scene;
}

namespace NextAstrobot
{
    enum class EMechanismKind : uint8_t
    {
        MovingPlatform,
        Pendulum,
        Seesaw,
        SpinDisc,
        Crumble,
        BouncePad,
        Spring,
        Roller,
        Conveyor,
        Zipline,
        Button,
        Gate,
        Cage,
        SpikeBall,
        LaserBeam,
        Fan,
        Fountain,
        Windmill,
        ChestLid,
        Lever,
        CheckpointFlag,
        Count,
    };

    enum class EHazardKind : uint8_t
    {
        Lava,
        Water,
        Spikes,
        Laser,
        SpikeBall,
    };

    enum class EEnemyKind : uint8_t
    {
        Walker,
        Flyer,
        Spiky,
    };

    enum class EInteractableKind : uint8_t
    {
        Crate,
        BrickWall,
        Chest,
        LostBot,
    };

    enum class ECollectibleKind : uint8_t
    {
        Coin,
        Puzzle,
        Gem,
        Key,
        Star,
    };

    const char* MechanismKindName(EMechanismKind kind);

    /// One kit module call, resolved to a live scene node plus its authoring parameters.
    struct FIndexedNode
    {
        Assets::Node* node = nullptr;
        /// The module's ab_part_* child, when it has one. A hazard whose lethal volume
        /// moves (the swinging spike ball) or switches off (the blinking laser) reads it
        /// straight off this node instead of the bind-pose transform.
        Assets::Node* part = nullptr;
        uint32_t id = 0;
        Assets::Scad::FMetadata meta;
        glm::vec3 worldPos{0.0f};
        glm::quat worldRot{1.0f, 0.0f, 0.0f, 0.0f};

        double Number(std::string_view key, double fallback) const
        {
            return Assets::Scad::MetadataNumber(meta, key, fallback);
        }
        bool Bool(std::string_view key, bool fallback) const
        {
            return Assets::Scad::MetadataBool(meta, key, fallback);
        }
        /// World-space direction the module's local +x axis points at (linear kit pieces
        /// extend along +x, so this is the rail / belt / zipline direction).
        glm::vec3 AxisX() const { return worldRot * glm::vec3(1.0f, 0.0f, 0.0f); }
        /// World-space direction of the module's local SCAD -y, i.e. its "front".
        glm::vec3 AxisFront() const { return worldRot * glm::vec3(0.0f, 0.0f, 1.0f); }
    };

    struct FMechanismRecord
    {
        EMechanismKind kind = EMechanismKind::Count;
        FIndexedNode root;
        /// The ab_part_* child the game animates; null for mechanisms that have none
        /// (spin disc, bounce pad, conveyor).
        Assets::Node* part = nullptr;
        glm::vec3 partBindTranslation{0.0f};
        glm::quat partBindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct FTypedNode
    {
        FIndexedNode node;
        uint8_t kind = 0;
    };

    struct FLevelIndex
    {
        std::vector<FIndexedNode> coins, puzzles, gems, keys, stars;
        std::vector<FMechanismRecord> mechanisms;
        std::vector<FTypedNode> hazards;     // kind = EHazardKind
        std::vector<FTypedNode> enemies;     // kind = EEnemyKind
        std::vector<FTypedNode> interactables; // kind = EInteractableKind
        std::vector<FIndexedNode> checkpoints;
        std::vector<FIndexedNode> cages;
        FIndexedNode spawn;
        FIndexedNode goal;
        bool hasSpawn = false;
        bool hasGoal = false;
        /// Lowest island top surface in the level; the kill plane hangs below it.
        float lowestGroundY = 0.0f;

        static FLevelIndex Build(Assets::Scene& scene, std::vector<std::string>* outWarnings);

        size_t RescueTotal() const { return cages.size() + CountInteractables(EInteractableKind::LostBot); }
        size_t CountInteractables(EInteractableKind kind) const;
        const FMechanismRecord* FindMechanism(EMechanismKind kind) const;
    };
}
