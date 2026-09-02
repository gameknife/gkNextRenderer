#include "Application/Game/NextAstrobot/World/CollectibleSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    namespace
    {
        constexpr float kSpinDegreesPerSecond = 110.0f;
        // Pickups sit above their node origin (the kit's `hover` parameter); testing
        // against the player's chest rather than their feet keeps a coin row at
        // running height reachable without crouching the capsule into it.
        constexpr float kPlayerChestHeight = 0.8f;

        void SetItemVisible(Assets::Node* node, bool visible)
        {
            if (!node)
            {
                return;
            }
            if (auto* render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(visible);
            }
        }
    }

    void FCollectibleSystem::Unbind()
    {
        items_.clear();
        coinsTotal_ = 0;
        puzzlesTotal_ = 0;
    }

    void FCollectibleSystem::Bind(const FLevelIndex& index)
    {
        Unbind();
        const auto add = [this](const std::vector<FIndexedNode>& source, ECollectibleKind kind, bool spins)
        {
            for (const FIndexedNode& entry : source)
            {
                FItem item;
                item.node = entry.node;
                item.position = entry.worldPos;
                item.bindRotation = entry.node ? entry.node->Rotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                item.kind = kind;
                item.spins = spins;
                items_.push_back(item);
            }
        };
        add(index.coins, ECollectibleKind::Coin, true);
        add(index.puzzles, ECollectibleKind::Puzzle, true);
        add(index.gems, ECollectibleKind::Gem, true);
        add(index.keys, ECollectibleKind::Key, true);
        // The star sits on the goal arch and reads better bobbing than spinning flat.
        add(index.stars, ECollectibleKind::Star, false);

        coinsTotal_ = static_cast<int>(index.coins.size());
        puzzlesTotal_ = static_cast<int>(index.puzzles.size());
    }

    int FCollectibleSystem::Remaining() const
    {
        return static_cast<int>(std::count_if(items_.begin(), items_.end(),
                                              [](const FItem& item) { return !item.taken; }));
    }

    FPickupEvent FCollectibleSystem::Update(float time, float /*deltaSeconds*/, const glm::vec3& playerFoot)
    {
        FPickupEvent event;
        const glm::vec3 chest = playerFoot + glm::vec3(0.0f, kPlayerChestHeight, 0.0f);
        const float radiusSq = config_.PickupRadius * config_.PickupRadius;

        for (FItem& item : items_)
        {
            if (item.taken || !item.node)
            {
                continue;
            }

            if (item.spins)
            {
                item.node->SetRotation(item.bindRotation *
                                       glm::angleAxis(glm::radians(time * kSpinDegreesPerSecond),
                                                      glm::vec3(0.0f, 1.0f, 0.0f)));
            }

            const glm::vec3 delta = item.position - chest;
            if (glm::dot(delta, delta) > radiusSq)
            {
                continue;
            }

            item.taken = true;
            SetItemVisible(item.node, false);
            switch (item.kind)
            {
            case ECollectibleKind::Coin: ++event.coins; break;
            case ECollectibleKind::Puzzle: ++event.puzzles; break;
            // A gem is worth ten coins, and is also tallied on its own.
            case ECollectibleKind::Gem: ++event.gems; event.coins += 10; break;
            case ECollectibleKind::Key: ++event.keys; break;
            case ECollectibleKind::Star: event.star = true; break;
            }
        }
        return event;
    }
}
