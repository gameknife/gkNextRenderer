#pragma once

#include "Battle/BattleState.h"
#include "NextTotalwarCombatConfig.hpp"
#include "Gameplay/Rig/RigInstance.h"

#include <array>
#include <memory>
#include <vector>

namespace NextTotalwar
{
    struct FSoldierVisual
    {
        std::shared_ptr<Assets::Node> worldNode;
        std::vector<Assets::Node*> renderNodes;
        std::vector<std::array<uint32_t, 16>> baseMaterials;
        NextGameplay::FRigAnimator animator;
        bool flashApplied = false;
    };

    class FCombatFx
    {
    public:
        void Initialize(uint32_t flashMaterialId,
                        std::vector<std::shared_ptr<Assets::Node>> bloodPool);
        void Reset();
        void Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                  std::vector<std::vector<FSoldierVisual>>& visuals,
                  const FCombatTuning& tuning,
                  const std::vector<FCombatEvent>& events);

        int CorpseCount() const { return corpseCount_; }
        int FlashingCount() const { return flashingCount_; }
        int BloodStainCount() const { return bloodStainCount_; }
        size_t BloodPoolCapacity() const { return bloodPool_.size(); }

    private:
        void SetFlashing(FSoldierVisual& visual, bool flashing) const;
        void SpawnBloodStain(const FCombatEvent& event);

        uint32_t flashMaterialId_ = 0;
        std::vector<std::shared_ptr<Assets::Node>> bloodPool_;
        size_t nextBloodSlot_ = 0;
        uint32_t bloodSpawnCount_ = 0;
        int corpseCount_ = 0;
        int flashingCount_ = 0;
        int bloodStainCount_ = 0;
    };
}
