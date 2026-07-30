#pragma once

#include "Battle/BattleState.h"
#include "Gameplay/Rig/RigInstance.h"

#include <memory>
#include <vector>

namespace NextTotalwar
{
    struct FSoldierVisual
    {
        std::shared_ptr<Assets::Node> worldNode;
        std::vector<Assets::Node*> renderNodes;
        NextGameplay::FRigAnimator animator;
    };

    class FCombatFx
    {
    public:
        void Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                  std::vector<std::vector<FSoldierVisual>>& visuals,
                  const std::vector<FCombatEvent>& events);

        int CorpseCount() const { return corpseCount_; }

    private:
        int corpseCount_ = 0;
    };
}
