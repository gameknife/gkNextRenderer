#include "Engine/Common/CoreMinimal.hpp"

#include "WorldAnchorRegistry.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    namespace
    {
        void DisableMarker(Assets::Node* node, NextPhysics* physics)
        {
            if (!node)
            {
                return;
            }
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(false);
            }
            if (auto component = node->GetComponent<Runtime::PhysicsComponent>(); component && physics)
            {
                physics->SetBodyActive(component->GetPhysicsBody(), false);
            }
            for (const auto& child : node->Children())
            {
                DisableMarker(child.get(), physics);
            }
        }

        std::optional<std::pair<EWorldAnchorType, std::string>> Parse(std::string_view name)
        {
            if (name == "cw_prop_well")
            {
                return std::pair{EWorldAnchorType::Well, std::string("water")};
            }
            constexpr std::string_view prefix = "nd_spawn_";
            if (!name.starts_with(prefix))
            {
                return std::nullopt;
            }
            name.remove_prefix(prefix.size());
            if (name == "player_safe")
            {
                return std::pair{EWorldAnchorType::PlayerSafe, std::string("safe")};
            }
            constexpr std::string_view zombie = "zombie_";
            if (name.starts_with(zombie))
            {
                name.remove_prefix(zombie.size());
                return std::pair{EWorldAnchorType::Zombie, std::string(name)};
            }
            constexpr std::string_view loot = "loot_";
            if (name.starts_with(loot))
            {
                name.remove_prefix(loot.size());
                return std::pair{EWorldAnchorType::Loot, std::string(name)};
            }
            return std::nullopt;
        }
    }

    void WorldAnchorRegistry::Scan(NextEngine& engine)
    {
        Clear();
        Assets::Scene& scene = engine.GetScene();
        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }
            const auto parsed = Parse(node->GetName());
            if (!parsed)
            {
                continue;
            }
            node->RecalcTransform(true);
            anchors_.push_back({parsed->first, parsed->second, node->WorldTranslation(), node->GetInstanceId()});
            if (parsed->first != EWorldAnchorType::Well)
            {
                DisableMarker(node.get(), engine.GetPhysicsEngine());
            }
        }
        scene.MarkDirty();
        SPDLOG_INFO("[NextDayz] registered {} world anchors (generation {})", anchors_.size(), generation_);
    }

    void WorldAnchorRegistry::Clear()
    {
        anchors_.clear();
        ++generation_;
        if (generation_ == 0)
        {
            ++generation_;
        }
    }

    const FWorldAnchor* WorldAnchorRegistry::Resolve(FWorldAnchorHandle handle) const
    {
        if (!handle.IsValid() || handle.generation != generation_ || handle.index >= anchors_.size())
        {
            return nullptr;
        }
        return &anchors_[handle.index];
    }

    std::vector<FWorldAnchorHandle> WorldAnchorRegistry::Find(EWorldAnchorType type) const
    {
        std::vector<FWorldAnchorHandle> result;
        for (size_t index = 0; index < anchors_.size(); ++index)
        {
            if (anchors_[index].type == type)
            {
                result.push_back({static_cast<uint32_t>(index), generation_});
            }
        }
        return result;
    }

    size_t WorldAnchorRegistry::Count(EWorldAnchorType type) const
    {
        return static_cast<size_t>(std::count_if(anchors_.begin(), anchors_.end(),
            [type](const FWorldAnchor& anchor) { return anchor.type == type; }));
    }
}
