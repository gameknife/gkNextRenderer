#pragma once

#include "Battle/BattleState.h"

#include <memory>
#include <vector>

namespace Assets
{
    class Node;
}

namespace NextTotalwar
{
    class FRangedVolleyFx
    {
    public:
        void Initialize(std::vector<std::shared_ptr<Assets::Node>> arrowPool);
        void ResetBattle();
        void Tick(float deltaSeconds, const std::vector<FRegiment>& regiments,
                  const std::vector<FCombatEvent>& events);

        [[nodiscard]] size_t PoolCapacity() const { return arrows_.size(); }
        [[nodiscard]] int ActiveCount() const;

    private:
        struct FArrow
        {
            std::shared_ptr<Assets::Node> node;
            glm::vec3 start{};
            glm::vec3 end{};
            float age = 0.0f;
            float duration = 0.0f;
            float arcHeight = 0.0f;
            bool active = false;
        };

        void SpawnVolley(const FCombatEvent& event, const std::vector<FRegiment>& regiments);

        std::vector<FArrow> arrows_;
        size_t nextArrow_ = 0;
        uint32_t volleySerial_ = 0;
    };
}
