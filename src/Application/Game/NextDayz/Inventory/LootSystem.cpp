#include "LootSystem.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    namespace
    {
        FLootGrant Weapon(std::string id, std::string display)
        {
            return FLootGrant{std::move(id), std::move(display), EItemKind::Weapon, 1};
        }
        FLootGrant Ammo(EAmmoType type, int count)
        {
            return FLootGrant{std::string(AmmoItemId(type)), std::string(AmmoDisplayName(type)), EItemKind::Ammo, count};
        }
        FLootGrant Clothing(std::string id, std::string display)
        {
            return FLootGrant{std::move(id), std::move(display), EItemKind::Clothing, 1};
        }
        FLootGrant Misc(std::string id, std::string display, int count = 1)
        {
            return FLootGrant{std::move(id), std::move(display), EItemKind::Misc, count};
        }

        // Map node name (scad module) -> game item(s). Built once, shared by all
        // entries. Nodes not in the table are ignored.
        const std::unordered_map<std::string, FLootDef>& LootTable()
        {
            static const std::unordered_map<std::string, FLootDef> table = {
                {"cw_wpn_ak",           {"AK-74",        {Weapon("ak", "AK-74")}}},
                {"cw_wpn_svd",          {"SVD",          {Weapon("svd", "SVD")}}},
                {"cw_wpn_mosin",        {"Mosin",        {Weapon("mosin", "Mosin")}}},
                {"cw_wpn_shotgun",      {"Shotgun",      {Weapon("shotgun", "Shotgun")}}},
                {"cw_wpn_pistol",       {"Makarov",      {Weapon("pistol", "Makarov")}}},
                {"cw_wpn_crate",        {"Weapon Crate", {Weapon("ak", "AK-74"), Ammo(EAmmoType::Rifle545, 30)}}},
                {"cw_prop_crate_ammo",  {"Ammo Crate",   {Ammo(EAmmoType::Rifle545, 30)}}},
                {"cw_item_ammobox",     {"Ammo Box",     {Ammo(EAmmoType::Rifle762, 20)}}},
                {"cw_item_helmet",      {"Helmet",       {Clothing("helmet", "Helmet")}}},
                {"cw_item_backpack",    {"Backpack",     {Clothing("backpack", "Backpack")}}},
                {"cw_item_medkit",      {"Medkit",       {Misc("medkit", "Medkit")}}},
                {"cw_item_can",         {"Canned Food",  {Misc("food_can", "Canned Food")}}},
                {"cw_item_jerrycan",    {"Jerry Can",    {Misc("fuel", "Fuel")}}},
                {"cw_item_radio",       {"Radio",        {Misc("radio", "Radio")}}},
                {"cw_item_crate_supply",{"Supply Crate", {Misc("food_can", "Canned Food", 2), Misc("bandage", "Bandage", 2)}}},
                {"cw_item_bedroll",     {"Bedroll",      {Misc("bedroll", "Bedroll")}}},
                {"cw_item_lantern",     {"Lantern",      {Misc("lantern", "Lantern")}}},
            };
            return table;
        }

        void HideNodeTree(Assets::Node* node, NextPhysics* physics)
        {
            if (!node)
            {
                return;
            }
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(false);
            }
            if (auto phys = node->GetComponent<Runtime::PhysicsComponent>())
            {
                if (physics)
                {
                    physics->SetBodyActive(phys->GetPhysicsBody(), false);
                }
            }
            for (const auto& child : node->Children())
            {
                HideNodeTree(child.get(), physics);
            }
        }

        void ShowNodeTree(Assets::Node* node, NextPhysics* physics)
        {
            if (!node)
            {
                return;
            }
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(true);
            }
            if (auto phys = node->GetComponent<Runtime::PhysicsComponent>(); phys && physics)
            {
                physics->SetBodyActive(phys->GetPhysicsBody(), true);
            }
            for (const auto& child : node->Children())
            {
                ShowNodeTree(child.get(), physics);
            }
        }

        ELootCategory CategoryFor(const FLootDef& definition)
        {
            if (definition.grants.empty())
            {
                return ELootCategory::Misc;
            }
            const FLootGrant& grant = definition.grants.front();
            const FItemDef* item = FindItemDef(grant.id);
            const EItemKind kind = item ? item->kind : grant.kind;
            if (grant.id == "food_can" || grant.id == "water_bottle") return ELootCategory::FoodWater;
            if (grant.id == "medkit" || grant.id == "bandage") return ELootCategory::Medical;
            if (kind == EItemKind::Weapon || kind == EItemKind::Melee) return ELootCategory::Weapon;
            if (kind == EItemKind::Ammo) return ELootCategory::Ammo;
            if (kind == EItemKind::Clothing) return ELootCategory::Clothing;
            return ELootCategory::Misc;
        }

        ELootProfile ProfileFor(const FLootDef& definition)
        {
            const ELootCategory category = CategoryFor(definition);
            if (category == ELootCategory::Medical) return ELootProfile::Medical;
            if (category == ELootCategory::Ammo || category == ELootCategory::Weapon) return ELootProfile::Military;
            return ELootProfile::Residential;
        }
    }

    void LootSystem::OnSceneLoaded(Assets::Scene& scene)
    {
        entries_.clear();
        hoveredIndex_ = -1;
        ++generation_;
        if (generation_ == 0)
        {
            ++generation_;
        }
        worldSeconds_ = 0.0;
        director_.Reset(generation_, seed_);

        const auto& table = LootTable();
        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }
            const auto it = table.find(node->GetName());
            if (it == table.end())
            {
                continue;
            }
            node->RecalcTransform(true);
            FLootEntry entry;
            entry.nodeInstanceId = node->GetInstanceId();
            entry.worldPos = node->WorldTranslation();
            entry.def = &it->second;
            entry.directorHandle = director_.AddSlot(entry.worldPos, ProfileFor(*entry.def), CategoryFor(*entry.def));
            entries_.push_back(entry);
        }
        SPDLOG_INFO("[NextDayz] registered {} loot entries", entries_.size());
    }

    void LootSystem::OnSceneUnloaded()
    {
        entries_.clear();
        hoveredIndex_ = -1;
        ++generation_;
        director_.Reset(generation_, seed_);
    }

    void LootSystem::ResetSession(NextEngine& engine)
    {
        for (const FLootEntry& entry : entries_)
        {
            if (Assets::Node* node = engine.GetScene().GetNodeByInstanceId(entry.nodeInstanceId))
            {
                ShowNodeTree(node, engine.GetPhysicsEngine());
            }
        }
        engine.GetScene().MarkDirty();
        OnSceneLoaded(engine.GetScene());
    }

    bool LootSystem::NearestLootPos(const glm::vec2& approxXZ, glm::vec3& outWorldPos) const
    {
        float best = std::numeric_limits<float>::max();
        bool found = false;
        for (const FLootEntry& entry : entries_)
        {
            const glm::vec2 xz(entry.worldPos.x, entry.worldPos.z);
            const float d = glm::dot(xz - approxXZ, xz - approxXZ);
            if (d < best)
            {
                best = d;
                outWorldPos = entry.worldPos;
                found = true;
            }
        }
        return found;
    }

    void LootSystem::Update(float deltaSeconds, const glm::vec3& eyePosition, const glm::vec3& viewForward,
                            NextEngine& engine)
    {
        worldSeconds_ += std::max(deltaSeconds, 0.0f);
        hoveredIndex_ = -1;
        float bestDot = config_.AimDotMin;
        for (size_t i = 0; i < entries_.size(); ++i)
        {
            FLootEntry& entry = entries_[i];
            const glm::vec3 delta = entry.worldPos - eyePosition;
            const float distance = glm::length(delta);
            const float viewDot = distance > 0.01f ? glm::dot(delta / distance, viewForward) : 1.0f;
            if (entry.state == FLootEntry::EState::Cooldown)
            {
                int categoryAvailable = 0;
                const FLootSlot* current = director_.Resolve(entry.directorHandle);
                if (current)
                {
                    for (const FLootEntry& other : entries_)
                    {
                        const FLootSlot* otherSlot = director_.Resolve(other.directorHandle);
                        if (other.state == FLootEntry::EState::Available && otherSlot &&
                            otherSlot->category == current->category)
                        {
                            ++categoryAvailable;
                        }
                    }
                }
                const bool visible = viewDot > config_.AimDotMin && distance < 140.0f;
                if (director_.EvaluateRespawn(entry.directorHandle, worldSeconds_, distance, visible,
                                              categoryAvailable, 24))
                {
                    entry.state = FLootEntry::EState::Available;
                    if (Assets::Node* node = engine.GetScene().GetNodeByInstanceId(entry.nodeInstanceId))
                    {
                        ShowNodeTree(node, engine.GetPhysicsEngine());
                        engine.GetScene().MarkDirty();
                    }
                }
            }
            if (entry.state != FLootEntry::EState::Available)
            {
                continue;
            }
            if (distance > config_.ReachMeters || distance < 0.01f)
            {
                continue;
            }
            const float dot = glm::dot(delta / distance, viewForward);
            if (dot > bestDot)
            {
                bestDot = dot;
                hoveredIndex_ = static_cast<int>(i);
            }
        }
    }

    const FLootEntry* LootSystem::Hovered() const
    {
        if (hoveredIndex_ < 0 || hoveredIndex_ >= static_cast<int>(entries_.size()))
        {
            return nullptr;
        }
        return &entries_[hoveredIndex_];
    }

    std::string LootSystem::HoveredPrompt() const
    {
        const FLootEntry* entry = Hovered();
        return (entry && entry->def) ? entry->def->displayName : std::string();
    }

    std::optional<FLootHandle> LootSystem::ReserveHovered()
    {
        if (hoveredIndex_ < 0 || hoveredIndex_ >= static_cast<int>(entries_.size()))
        {
            return std::nullopt;
        }
        FLootEntry& entry = entries_[hoveredIndex_];
        if (entry.state != FLootEntry::EState::Available || !entry.def)
        {
            return std::nullopt;
        }
        entry.state = FLootEntry::EState::Reserved;
        if (!director_.Reserve(entry.directorHandle))
        {
            entry.state = FLootEntry::EState::Available;
            return std::nullopt;
        }
        const FLootHandle handle{static_cast<uint32_t>(hoveredIndex_), entry.nodeInstanceId, generation_};
        hoveredIndex_ = -1;
        return handle;
    }

    bool LootSystem::IsReservedHandleValid(const FLootHandle& handle) const
    {
        if (!handle.IsValid() || handle.generation != generation_ || handle.index >= entries_.size())
        {
            return false;
        }
        const FLootEntry& entry = entries_[handle.index];
        const FLootSlot* slot = director_.Resolve(entry.directorHandle);
        return entry.nodeInstanceId == handle.nodeInstanceId && entry.state == FLootEntry::EState::Reserved &&
               slot && slot->state == ELootSlotState::Reserved;
    }

    const FLootDef* LootSystem::Commit(const FLootHandle& handle, Inventory& inventory, NextEngine& engine)
    {
        if (!IsReservedHandleValid(handle))
        {
            return nullptr;
        }
        FLootEntry& entry = entries_[handle.index];
        std::vector<FInventoryAddRequest> requests;
        requests.reserve(entry.def->grants.size());
        for (const FLootGrant& grant : entry.def->grants)
        {
            requests.push_back({grant.id, grant.displayName, grant.kind, grant.count});
        }
        if (!inventory.TryAddBatch(requests))
        {
            entry.state = FLootEntry::EState::Available;
            director_.Cancel(entry.directorHandle);
            return nullptr;
        }

        Assets::Scene& scene = engine.GetScene();
        if (Assets::Node* node = scene.GetNodeByInstanceId(entry.nodeInstanceId))
        {
            HideNodeTree(node, engine.GetPhysicsEngine());
            scene.MarkDirty();
        }

        if (!director_.Commit(entry.directorHandle, worldSeconds_))
        {
            return nullptr;
        }
        entry.state = FLootEntry::EState::Cooldown;
        hoveredIndex_ = -1;
        return entry.def;
    }

    bool LootSystem::Cancel(const FLootHandle& handle)
    {
        if (!IsReservedHandleValid(handle))
        {
            return false;
        }
        entries_[handle.index].state = FLootEntry::EState::Available;
        return director_.Cancel(entries_[handle.index].directorHandle);
    }

    int LootSystem::RemainingCount() const
    {
        int remaining = 0;
        for (const FLootEntry& entry : entries_)
        {
            if (entry.state == FLootEntry::EState::Available || entry.state == FLootEntry::EState::Reserved)
            {
                ++remaining;
            }
        }
        return remaining;
    }

    int LootSystem::CooldownCount() const
    {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [](const FLootEntry& entry)
        {
            return entry.state == FLootEntry::EState::Cooldown;
        }));
    }
}
