#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Sim/ScadRigVisual.h"
#include "Gameplay/Sim/SimCharacter.h"

#include <array>
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
