#include "Application/Game/NextAstrobot/World/InteractableSystem.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>

#include <fmt/format.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismSystem.hpp"

namespace NextAstrobot
{
    namespace
    {
        // A kit prop keeps its geometry in the material-group children the SCAD loader made,
        // so the module node itself usually draws nothing: touching only that node leaves a
        // smashed crate standing there, still blocking the way.
        void SetInteractableVisible(Assets::Node* node, bool visible)
        {
            Assets::NodeUtils::SetVisibleRecursive(node, visible);
            // A smashed crate must stop blocking the way, not just disappear.
            Assets::NodeUtils::SetRayCastVisibleRecursive(node, visible);
        }

        float BreakableRadius(EInteractableKind kind)
        {
            switch (kind)
            {
            case EInteractableKind::Crate: return 1.0f;
            case EInteractableKind::BrickWall: return 2.6f;
            case EInteractableKind::Chest: return 1.1f;
            case EInteractableKind::LostBot: return 1.0f;
            }
            return 1.0f;
        }

        Assets::Node* FindChildNamed(Assets::Node& parent, std::string_view name)
        {
            for (const std::shared_ptr<Assets::Node>& child : parent.Children())
            {
                if (child->GetName() == name)
                {
                    return child.get();
                }
            }
            return nullptr;
        }

        int CoinsFromSmash(EInteractableKind kind)
        {
            switch (kind)
            {
            case EInteractableKind::Crate: return 3;
            case EInteractableKind::BrickWall: return 0;
            case EInteractableKind::Chest: return 5;
            default: return 0;
            }
        }
    }

    void FInteractableSystem::Unbind()
    {
        mechanisms_ = nullptr;
        breakables_.clear();
        rescues_.clear();
        checkpoints_.clear();
        levers_.clear();
        hasGoal_ = false;
        goalReached_ = false;
        rescueTotal_ = 0;
        activeCheckpoint_ = -1;
    }

    void FInteractableSystem::SetSpawn(const glm::vec3& position, float yaw)
    {
        respawnPosition_ = position;
        respawnYaw_ = yaw;
    }

    void FInteractableSystem::Bind(const FLevelIndex& index, FMechanismSystem& mechanisms)
    {
        const glm::vec3 spawn = respawnPosition_;
        const float spawnYaw = respawnYaw_;
        Unbind();
        respawnPosition_ = spawn;
        respawnYaw_ = spawnYaw;
        mechanisms_ = &mechanisms;

        for (const FTypedNode& entry : index.interactables)
        {
            const auto kind = static_cast<EInteractableKind>(entry.kind);
            if (kind == EInteractableKind::LostBot)
            {
                FRescue rescue;
                rescue.node = entry.node.node;
                rescue.botNode = entry.node.node;
                rescue.position = entry.node.worldPos;
                rescues_.push_back(rescue);
                continue;
            }
            FBreakable breakable;
            breakable.node = entry.node.node;
            breakable.position = entry.node.worldPos;
            breakable.kind = kind;
            breakable.radius = BreakableRadius(kind);
            breakables_.push_back(breakable);
        }

        for (const FIndexedNode& cage : index.cages)
        {
            FRescue rescue;
            rescue.node = cage.node;
            // The prisoner is the ab_char_bot the cage module puts on its floor; it is
            // what gets swapped for an animated rig once the dome comes off.
            rescue.botNode = cage.node ? FindChildNamed(*cage.node, "ab_char_bot") : nullptr;
            rescue.inCage = true;
            rescue.position = cage.worldPos;
            rescues_.push_back(rescue);
        }
        rescueTotal_ = static_cast<int>(rescues_.size());

        for (const FIndexedNode& checkpoint : index.checkpoints)
        {
            checkpoints_.push_back({checkpoint.node, checkpoint.worldPos,
                                    static_cast<int>(checkpoint.Number("idx", 0.0)), false});
        }

        // Levers live in the mechanism table (they are an ab_part_* piece), but the punch
        // test belongs here with the crates and the cages, which run after the player has
        // moved and therefore see this frame's punch rather than last frame's.
        for (const FMechanismRecord& record : index.mechanisms)
        {
            if (record.kind == EMechanismKind::Lever)
            {
                levers_.push_back({record.root.node, record.root.worldPos, false});
            }
        }

        hasGoal_ = index.hasGoal;
        if (hasGoal_)
        {
            goalPosition_ = index.goal.worldPos;
        }
    }

    FInteractionEvent FInteractableSystem::Update(float deltaSeconds, const glm::vec3& playerFoot, bool punchStarted,
                                                  const glm::vec3& punchOrigin, const glm::vec3& punchDirection,
                                                  float punchRange, float punchArcDegrees)
    {
        FInteractionEvent event;
        const float punchCos = std::cos(glm::radians(punchArcDegrees * 0.5f));
        const glm::vec3 punchForward =
            glm::length(punchDirection) > 0.001f ? glm::normalize(punchDirection) : glm::vec3(0.0f, 0.0f, 1.0f);
        // The arc is a horizontal sector: what matters is whether the target is in front of
        // the player, not how tall it is. A cage the player is standing under, or a brick
        // wall taller than they are, would otherwise fall outside a full 3D cone.
        const auto inPunchCone = [&](const glm::vec3& target, float extraRadius)
        {
            const glm::vec3 toTarget = target - punchOrigin;
            if (glm::length(toTarget) > punchRange + extraRadius)
            {
                return false;
            }
            const glm::vec3 flat(toTarget.x, 0.0f, toTarget.z);
            const float flatLength = glm::length(flat);
            if (flatLength < 0.05f)
            {
                return true; // straight overhead or underfoot, and already within reach
            }
            return glm::dot(flat / flatLength, punchForward) >= punchCos;
        };

        if (punchStarted)
        {
            for (FBreakable& breakable : breakables_)
            {
                if (breakable.broken || !inPunchCone(breakable.position + glm::vec3(0.0f, 0.6f, 0.0f),
                                                     breakable.radius))
                {
                    continue;
                }
                breakable.broken = true;
                if (breakable.kind == EInteractableKind::Chest)
                {
                    // A chest is not destroyed, it is opened: the lid flips back and the
                    // box stays in the world as a landmark that this pocket is done.
                    if (mechanisms_)
                    {
                        mechanisms_->Latch(EMechanismKind::ChestLid, breakable.node);
                    }
                }
                else
                {
                    SetInteractableVisible(breakable.node, false);
                }
                ++event.smashed;
                event.coinsFromSmash += CoinsFromSmash(breakable.kind);
            }

            for (FLever& lever : levers_)
            {
                if (lever.pulled || !inPunchCone(lever.position + glm::vec3(0.0f, 0.6f, 0.0f), 0.8f))
                {
                    continue;
                }
                if (mechanisms_ && mechanisms_->Latch(EMechanismKind::Lever, lever.node))
                {
                    lever.pulled = true;
                    event.leverPulled = true;
                    event.toast = "Lever pulled!";
                }
            }
        }

        for (FRescue& rescue : rescues_)
        {
            if (rescue.freed)
            {
                continue;
            }
            if (rescue.inCage)
            {
                // Caged robots are freed by punching the cage open.
                if (punchStarted && mechanisms_ && inPunchCone(rescue.position + glm::vec3(0.0f, 1.0f, 0.0f), 1.4f) &&
                    mechanisms_->Latch(EMechanismKind::Cage, rescue.node))
                {
                    rescue.freed = true;
                    SetInteractableVisible(rescue.botNode, false);
                    ++event.rescued;
                    event.freed.push_back({rescue.position, true});
                    event.toast = "Robot freed!";
                }
                continue;
            }

            const glm::vec3 delta = rescue.position - playerFoot;
            const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
            if (distance <= config_.InteractRadius && std::abs(delta.y) < 2.0f)
            {
                rescue.hold += deltaSeconds;
                if (rescue.hold >= config_.RescueHoldSeconds)
                {
                    rescue.freed = true;
                    SetInteractableVisible(rescue.botNode, false);
                    ++event.rescued;
                    event.freed.push_back({rescue.position, false});
                    event.toast = "Robot rescued!";
                }
            }
            else
            {
                rescue.hold = 0.0f;
            }
        }

        for (FCheckpoint& checkpoint : checkpoints_)
        {
            if (checkpoint.reached)
            {
                continue;
            }
            const glm::vec3 delta = checkpoint.position - playerFoot;
            if (std::sqrt(delta.x * delta.x + delta.z * delta.z) <= config_.CheckpointRadius &&
                std::abs(delta.y) < 3.0f)
            {
                checkpoint.reached = true;
                if (mechanisms_)
                {
                    mechanisms_->Latch(EMechanismKind::CheckpointFlag, checkpoint.node);
                }
                activeCheckpoint_ = checkpoint.index;
                respawnPosition_ = checkpoint.position + glm::vec3(0.0f, 0.2f, 0.0f);
                event.checkpointReached = checkpoint.index;
                event.toast = fmt::format("Checkpoint {}", checkpoint.index);
            }
        }

        if (hasGoal_ && !goalReached_)
        {
            const glm::vec3 delta = goalPosition_ - playerFoot;
            if (std::sqrt(delta.x * delta.x + delta.z * delta.z) <= config_.GoalRadius && std::abs(delta.y) < 4.0f)
            {
                goalReached_ = true;
                event.goalReached = true;
            }
        }
        return event;
    }
}
