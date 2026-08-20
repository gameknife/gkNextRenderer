#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Sim/ScadRigVisual.h"
#include "Gameplay/Sim/SimCharacter.h"

#include <array>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace NextGameplay::Sim
{
    struct FCharacterPoolConfig
    {
        int poolCapacity = 32;
        float navCellSize = 0.45f;
        float agentRadius = 0.28f;
        float separationRadius = 0.6f;
        float separationStrength = 1.2f;
        float groundY = 0.15f;
        // Ground height under (x, z), for worlds that are not a single flat
        // floor. When set it replaces the groundY clamp everywhere: character
        // integration, MoveTo's goal, and the path search's reference height.
        // Unset (the default) keeps the flat-floor behaviour an office or a
        // terminal building wants.
        std::function<float(float x, float z, float currentY)> groundSampler;
        // Explicit nav region. Left empty the pool covers the whole scene AABB,
        // which is right for a building interior and ruinous on a square
        // kilometre of city: at the default cell size that is millions of
        // raycast columns. Bound it and slide it with RebuildNavGrid instead.
        glm::vec3 navWorldMin{0.0f};
        glm::vec3 navWorldMax{0.0f};
        // floorHeightTolerance is an absolute band around the query's reference
        // height, not one that propagates along a path: it exists to keep the
        // storeys of a building apart. On sloping ground it has to span the
        // region's relief or walking uphill reads as changing floor.
        float navFloorTolerance = 1.0f;
        float navMaxStepHeight = 0.35f;
        float navClearanceHeight = 1.7f;
        // 0 = scene AABB top + 5. Must clear every roof in the region: a ceiling
        // below one starts its down-ray inside the building.
        float navSampleCeiling = 0.0f;
        float navMaxSlopeAngle = 50.0f;
        glm::vec3 parkedPosition{0.0f, -100.0f, 0.0f};
        bool useRig = false;
        std::string rigPath;
        const Assets::FRigAsset* rigAsset = nullptr;
        glm::vec3 boxHalfMin{-0.25f, 0.0f, -0.25f};
        glm::vec3 boxHalfMax{0.25f, 1.6f, 0.25f};
        FRigVisualParams rigVisual;
        std::string nodeNamePrefix = "sim_character";
        std::vector<glm::vec3> slotTints;
    };

    class FCharacterPool
    {
    public:
        void Configure(const FCharacterPoolConfig& config);
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        // Sets the initial nav region without rebuilding it. This is useful for
        // worlds whose logical origin is not a walkable surface; call before
        // OnSceneLoaded() when the first window must be placed elsewhere.
        void SetNavWorldBounds(const glm::vec3& worldMin, const glm::vec3& worldMax)
        {
            config_.navWorldMin = worldMin;
            config_.navWorldMax = worldMax;
        }
        // Re-samples the nav grid over a new window. Slide it to follow a
        // character across a world too large to grid in one go; the config's
        // navWorldMin/Max are updated so later rebuilds keep the new region.
        void RebuildNavGrid(Assets::Scene& scene, const glm::vec3& worldMin, const glm::vec3& worldMax);
        // The floor tolerance a sliding window needs depends on the relief it
        // covers, so it is settable on its own. Configure() would take the pool
        // apart, visuals included.
        void SetNavFloorTolerance(float tolerance) { config_.navFloorTolerance = tolerance; }
        void Clear();

        FSimCharacter* Acquire(int slot, const glm::vec3& position, const glm::vec3& tintColor);
        void Release(FSimCharacter& character);

        bool MoveTo(FSimCharacter& character, const glm::vec3& target);
        void MoveAlong(FSimCharacter& character, std::vector<glm::vec3> waypoints);
        bool Arrived(const FSimCharacter& character) const;
        void Tick(float deltaSeconds);
        void Tick(float deltaSeconds, std::span<FSimCharacter*> characters);
        void Tick(float deltaSeconds, Assets::Scene& scene);
        void Tick(float deltaSeconds, Assets::Scene& scene, std::span<FSimCharacter*> characters);

        std::vector<FSimCharacter>& Characters() { return characters_; }
        const std::vector<FSimCharacter>& Characters() const { return characters_; }
        bool NavReady() const { return navReady_; }
        const NextGameplay::FNavGrid& NavGrid() const { return navGrid_; }

    private:
        void BuildNavGrid(Assets::Scene& scene);
        void SetSlotTint(int slot, const glm::vec3& tintColor);
        void TickCharacters(float deltaSeconds, std::span<FSimCharacter*> characters);
        float GroundAt(float x, float z, float currentY) const;

        FCharacterPoolConfig config_;
        NextGameplay::FNavGrid navGrid_;
        std::vector<FSimCharacter> characters_;
        std::vector<uint32_t> materialIds_;
        std::vector<uint32_t> modelIds_;

        Assets::FRigAsset ownedRigAsset_;
        const Assets::FRigAsset* rigAsset_ = nullptr;
        bool rigLoaded_ = false;
        std::vector<std::vector<uint32_t>> rigSlotPartModelIds_;
        std::vector<std::array<uint32_t, 16>> rigBaseMaterials_;
        std::vector<uint32_t> rigSlotTintMaterials_;

        Assets::Scene* scene_ = nullptr;
        bool assetsInjected_ = false;
        bool navReady_ = false;
    };
}
