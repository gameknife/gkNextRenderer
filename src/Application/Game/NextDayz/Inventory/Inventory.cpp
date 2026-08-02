#include "Engine/Common/CoreMinimal.hpp"

#include "Inventory.hpp"

namespace NextDayz
{
    namespace
    {
        bool Accepts(const FInventoryContainer& container, const FItemDef& definition)
        {
            if (!container.active)
            {
                return false;
            }
            return !container.weaponOnly || definition.kind == EItemKind::Weapon;
        }
    }

    Inventory::Inventory()
    {
        Clear();
    }

    void Inventory::Clear()
    {
        instances_.clear();
        containers_.clear();
        legacyItems_.clear();
        equippedClothing_.clear();
        nextInstanceId_ = 1;
        nextContainerId_ = 1;
        containers_.push_back({nextContainerId_++, 0, "Basic shirt pockets", 6, true, false});
        containers_.push_back({nextContainerId_++, 0, "Basic trouser pockets", 4, true, false});
        containers_.push_back({nextContainerId_++, 0, "Primary weapon sling", 10, true, true});
        containers_.push_back({nextContainerId_++, 0, "Secondary weapon sling", 10, true, true});
    }

    int Inventory::InstanceVolume(const FItemInstance& instance)
    {
        const FItemDef* definition = FindItemDef(instance.defId);
        if (!definition || instance.count <= 0)
        {
            return 0;
        }
        const int stackCount = (instance.count + definition->maxStack - 1) / definition->maxStack;
        return stackCount * definition->volumePerStack;
    }

    const FItemInstance* Inventory::FindInstance(FItemInstanceId instanceId) const
    {
        const auto it = std::find_if(instances_.begin(), instances_.end(),
            [instanceId](const FItemInstance& item) { return item.instanceId == instanceId; });
        return it == instances_.end() ? nullptr : &*it;
    }

    FItemInstance* Inventory::FindInstance(FItemInstanceId instanceId)
    {
        return const_cast<FItemInstance*>(std::as_const(*this).FindInstance(instanceId));
    }

    bool Inventory::SetLoadedAmmo(FItemInstanceId instanceId, int loadedAmmo)
    {
        FItemInstance* instance = FindInstance(instanceId);
        const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
        if (!instance || !definition || definition->kind != EItemKind::Weapon || loadedAmmo < 0)
        {
            return false;
        }
        instance->loadedAmmo = loadedAmmo;
        return true;
    }

    int Inventory::LoadedAmmo(FItemInstanceId instanceId) const
    {
        const FItemInstance* instance = FindInstance(instanceId);
        return instance ? instance->loadedAmmo : -1;
    }

    const FInventoryContainer* Inventory::FindContainer(FContainerId containerId) const
    {
        const auto it = std::find_if(containers_.begin(), containers_.end(),
            [containerId](const FInventoryContainer& container) { return container.containerId == containerId; });
        return it == containers_.end() ? nullptr : &*it;
    }

    int Inventory::ContainerUsed(FContainerId containerId) const
    {
        int used = 0;
        for (const FItemInstance& instance : instances_)
        {
            if (instance.containerId == containerId)
            {
                used += InstanceVolume(instance);
            }
        }
        return used;
    }

    int Inventory::UsedCapacity() const
    {
        int used = 0;
        for (const FInventoryContainer& container : containers_)
        {
            if (container.active && !container.weaponOnly)
            {
                used += ContainerUsed(container.containerId);
            }
        }
        return used;
    }

    int Inventory::TotalCapacity() const
    {
        int capacity = 0;
        for (const FInventoryContainer& container : containers_)
        {
            if (container.active && !container.weaponOnly)
            {
                capacity += container.capacity;
            }
        }
        return capacity;
    }

    FContainerId Inventory::FindContainerWithSpace(const FItemDef& definition, int count, FContainerId excluded) const
    {
        FItemInstance candidate{0, std::string(definition.id), count};
        const int volume = InstanceVolume(candidate);
        for (const FInventoryContainer& container : containers_)
        {
            if (container.containerId == excluded || !Accepts(container, definition))
            {
                continue;
            }
            if (ContainerUsed(container.containerId) + volume <= container.capacity)
            {
                return container.containerId;
            }
        }
        return 0;
    }

    bool Inventory::CanAdd(const std::string& id, int count) const
    {
        const FItemDef* definition = FindItemDef(id);
        if (!definition || count <= 0)
        {
            return false;
        }
        Inventory copy = *this;
        return copy.TryAdd(id, std::string(definition->displayName), definition->kind, count);
    }

    bool Inventory::TryAdd(const std::string& id, const std::string& displayName, EItemKind kind, int count,
                           FItemInstanceId* outInstanceId)
    {
        if (count <= 0 || instances_.size() >= kMaxSlots)
        {
            return false;
        }
        Inventory rollback = *this;
        const FItemDef& definition = ResolveItemDef(id, displayName, kind);
        int remaining = count;
        FItemInstanceId firstInstanceId = 0;

        if (definition.maxStack > 1)
        {
            for (FItemInstance& instance : instances_)
            {
                if (instance.defId != id || instance.count >= definition.maxStack)
                {
                    continue;
                }
                const int oldVolume = InstanceVolume(instance);
                const int add = std::min(remaining, definition.maxStack - instance.count);
                instance.count += add;
                const int volumeDelta = InstanceVolume(instance) - oldVolume;
                const FInventoryContainer* container = FindContainer(instance.containerId);
                if (!container || ContainerUsed(instance.containerId) > container->capacity)
                {
                    instance.count -= add;
                    continue;
                }
                remaining -= add;
                firstInstanceId = firstInstanceId == 0 ? instance.instanceId : firstInstanceId;
                if (remaining == 0)
                {
                    break;
                }
                (void)volumeDelta;
            }
        }

        while (remaining > 0)
        {
            const int stackCount = std::min(remaining, definition.maxStack);
            const FContainerId containerId = FindContainerWithSpace(definition, stackCount);
            if (containerId == 0 || instances_.size() >= kMaxSlots)
            {
                *this = std::move(rollback);
                return false;
            }
            const FItemInstanceId instanceId = nextInstanceId_++;
            instances_.push_back({instanceId, id, stackCount, containerId, 0});
            firstInstanceId = firstInstanceId == 0 ? instanceId : firstInstanceId;
            remaining -= stackCount;
        }

        if (outInstanceId)
        {
            *outInstanceId = firstInstanceId;
        }
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryAddBatch(std::span<const FInventoryAddRequest> requests)
    {
        Inventory transaction = *this;
        for (const FInventoryAddRequest& request : requests)
        {
            if (!transaction.TryAdd(request.id, request.displayName, request.kind, request.count))
            {
                return false;
            }
        }
        *this = std::move(transaction);
        return true;
    }

    void Inventory::Add(const std::string& id, const std::string& displayName, EItemKind kind, int count)
    {
        TryAdd(id, displayName, kind, count);
    }

    bool Inventory::TryMove(FItemInstanceId instanceId, FContainerId destination, int count)
    {
        FItemInstance* source = FindInstance(instanceId);
        const FInventoryContainer* target = FindContainer(destination);
        if (!source || !target || !target->active)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(source->defId);
        if (!definition || !Accepts(*target, *definition))
        {
            return false;
        }
        const int moveCount = count < 0 ? source->count : count;
        if (moveCount <= 0 || moveCount > source->count)
        {
            return false;
        }
        FItemInstance moving = *source;
        moving.count = moveCount;
        moving.containerId = destination;
        if (ContainerUsed(destination) + InstanceVolume(moving) > target->capacity)
        {
            return false;
        }
        if (moveCount == source->count)
        {
            source->containerId = destination;
        }
        else
        {
            source->count -= moveCount;
            moving.instanceId = nextInstanceId_++;
            instances_.push_back(std::move(moving));
        }
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TrySplit(FItemInstanceId instanceId, int count, FItemInstanceId& outSplitId)
    {
        FItemInstance* source = FindInstance(instanceId);
        if (!source || count <= 0 || count >= source->count || instances_.size() >= kMaxSlots)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(source->defId);
        if (!definition || definition->maxStack <= 1)
        {
            return false;
        }
        FItemInstance split = *source;
        split.instanceId = nextInstanceId_++;
        split.count = count;
        source->count -= count;
        const FInventoryContainer* container = FindContainer(source->containerId);
        if (!container || ContainerUsed(source->containerId) + InstanceVolume(split) > container->capacity)
        {
            source->count += count;
            --nextInstanceId_;
            return false;
        }
        outSplitId = split.instanceId;
        instances_.push_back(std::move(split));
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryEquip(FItemInstanceId instanceId)
    {
        FItemInstance* instance = FindInstance(instanceId);
        if (!instance)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(instance->defId);
        if (!definition || definition->equipSlot == EEquipSlot::None)
        {
            return false;
        }
        for (const std::string& equippedId : equippedClothing_)
        {
            const FItemDef* equippedDef = FindItemDef(equippedId);
            if (equippedDef && equippedDef->equipSlot == definition->equipSlot)
            {
                return false;
            }
        }
        if (definition->kind == EItemKind::Clothing)
        {
            equippedClothing_.push_back(instance->defId);
        }
        if (definition->containerCapacity > 0)
        {
            containers_.push_back({nextContainerId_++, instanceId, std::string(definition->displayName),
                                   definition->containerCapacity, true, false});
        }
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryUnequip(FItemInstanceId instanceId)
    {
        FItemInstance* instance = FindInstance(instanceId);
        if (!instance)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(instance->defId);
        if (!definition)
        {
            return false;
        }
        const auto equippedIt = std::find(equippedClothing_.begin(), equippedClothing_.end(), instance->defId);
        if (equippedIt == equippedClothing_.end())
        {
            return false;
        }
        const auto containerIt = std::find_if(containers_.begin(), containers_.end(),
            [instanceId](const FInventoryContainer& container) { return container.ownerItemInstanceId == instanceId; });
        Inventory transaction = *this;
        if (containerIt != containers_.end())
        {
            const FContainerId removedId = containerIt->containerId;
            std::vector<FItemInstanceId> contents;
            for (const FItemInstance& item : transaction.instances_)
            {
                if (item.containerId == removedId)
                {
                    contents.push_back(item.instanceId);
                }
            }
            for (FItemInstanceId contentId : contents)
            {
                FItemInstance* content = transaction.FindInstance(contentId);
                const FItemDef* contentDef = content ? FindItemDef(content->defId) : nullptr;
                const FContainerId destination = contentDef
                    ? transaction.FindContainerWithSpace(*contentDef, content->count, removedId) : 0;
                if (destination == 0 || !transaction.TryMove(contentId, destination))
                {
                    return false;
                }
            }
            transaction.containers_.erase(std::remove_if(transaction.containers_.begin(), transaction.containers_.end(),
                [removedId](const FInventoryContainer& container) { return container.containerId == removedId; }),
                transaction.containers_.end());
        }
        transaction.equippedClothing_.erase(std::remove(transaction.equippedClothing_.begin(),
            transaction.equippedClothing_.end(), instance->defId), transaction.equippedClothing_.end());
        transaction.RebuildLegacyItems();
        *this = std::move(transaction);
        return true;
    }

    bool Inventory::TryDrop(FItemInstanceId instanceId, int count)
    {
        FItemInstance* instance = FindInstance(instanceId);
        if (!instance)
        {
            return false;
        }
        if (IsClothingEquipped(instance->defId) && !TryUnequip(instanceId))
        {
            return false;
        }
        instance = FindInstance(instanceId);
        const int dropCount = count < 0 ? instance->count : count;
        if (dropCount <= 0 || dropCount > instance->count)
        {
            return false;
        }
        instance->count -= dropCount;
        RemoveEmptyInstances();
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryConsumeInstance(FItemInstanceId instanceId, int count)
    {
        FItemInstance* instance = FindInstance(instanceId);
        if (!instance || count <= 0 || instance->count < count || IsClothingEquipped(instance->defId))
        {
            return false;
        }
        instance->count -= count;
        RemoveEmptyInstances();
        RebuildLegacyItems();
        return true;
    }

    int Inventory::CountOf(const std::string& id) const
    {
        int total = 0;
        for (const FItemInstance& instance : instances_)
        {
            if (instance.defId == id)
            {
                total += instance.count;
            }
        }
        return total;
    }

    int Inventory::Consume(const std::string& id, int count)
    {
        int remaining = count;
        for (FItemInstance& instance : instances_)
        {
            if (instance.defId == id && remaining > 0)
            {
                const int take = std::min(remaining, instance.count);
                instance.count -= take;
                remaining -= take;
            }
        }
        RemoveEmptyInstances();
        RebuildLegacyItems();
        return count - remaining;
    }

    bool Inventory::IsClothingEquipped(const std::string& id) const
    {
        return std::find(equippedClothing_.begin(), equippedClothing_.end(), id) != equippedClothing_.end();
    }

    void Inventory::SetClothingEquipped(const std::string& id, bool equipped)
    {
        const auto it = std::find_if(instances_.begin(), instances_.end(),
            [&id](const FItemInstance& instance) { return instance.defId == id; });
        if (it == instances_.end())
        {
            return;
        }
        if (equipped)
        {
            TryEquip(it->instanceId);
        }
        else
        {
            TryUnequip(it->instanceId);
        }
    }

    void Inventory::RemoveEmptyInstances()
    {
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
            [](const FItemInstance& instance) { return instance.count <= 0; }), instances_.end());
    }

    void Inventory::RebuildLegacyItems()
    {
        legacyItems_.clear();
        for (const FItemInstance& instance : instances_)
        {
            const FItemDef* definition = FindItemDef(instance.defId);
            const std::string displayName = definition ? std::string(definition->displayName) : instance.defId;
            const EItemKind kind = definition ? definition->kind : EItemKind::Misc;
            auto stack = std::find_if(legacyItems_.begin(), legacyItems_.end(),
                [&instance, kind](const FItemStack& item)
                {
                    return item.id == instance.defId && kind != EItemKind::Weapon && kind != EItemKind::Clothing;
                });
            if (stack != legacyItems_.end())
            {
                stack->count += instance.count;
            }
            else
            {
                legacyItems_.push_back({instance.defId, displayName, kind, instance.count});
            }
        }
    }
}
