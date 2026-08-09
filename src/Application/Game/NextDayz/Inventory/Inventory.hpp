#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

#include <span>

namespace NextDayz
{
    using FItemInstanceId = uint64_t;
    using FContainerId = uint32_t;

    struct FItemStack
    {
        std::string id;
        std::string displayName;
        EItemKind kind = EItemKind::Misc;
        int count = 1;
    };

    struct FItemInstance
    {
        FItemInstanceId instanceId = 0;
        std::string defId;
        int count = 1;
        FContainerId containerId = 0;
        int loadedAmmo = 0;
    };

    struct FInventoryContainer
    {
        FContainerId containerId = 0;
        FItemInstanceId ownerItemInstanceId = 0;
        std::string displayName;
        int capacity = 0;
        bool active = true;
        bool weaponOnly = false;
    };

    struct FInventoryAddRequest
    {
        std::string id;
        std::string displayName;
        EItemKind kind = EItemKind::Misc;
        int count = 1;
    };

    class Inventory
    {
    public:
        static constexpr size_t kMaxSlots = 256;

        Inventory();
        void Clear();

        bool CanAdd(const std::string& id, int count = 1) const;
        bool TryAdd(const std::string& id, const std::string& displayName, EItemKind kind, int count,
                    FItemInstanceId* outInstanceId = nullptr);
        bool TryAddBatch(std::span<const FInventoryAddRequest> requests);

        // Compatibility entry point for the pre-productization callers. New code
        // should use TryAdd/TryAddBatch so capacity rejection is observable.
        void Add(const std::string& id, const std::string& displayName, EItemKind kind, int count);

        bool TryMove(FItemInstanceId instanceId, FContainerId destination, int count = -1);
        bool TrySplit(FItemInstanceId instanceId, int count, FItemInstanceId& outSplitId);
        bool TryEquip(FItemInstanceId instanceId);
        bool TryUnequip(FItemInstanceId instanceId);
        bool TryDrop(FItemInstanceId instanceId, int count = -1);
        bool TryConsumeInstance(FItemInstanceId instanceId, int count = 1);

        const std::vector<FItemStack>& Items() const { return legacyItems_; }
        const std::vector<FItemInstance>& Instances() const { return instances_; }
        const std::vector<FInventoryContainer>& Containers() const { return containers_; }

        const FItemInstance* FindInstance(FItemInstanceId instanceId) const;
        FItemInstance* FindInstance(FItemInstanceId instanceId);
        bool SetLoadedAmmo(FItemInstanceId instanceId, int loadedAmmo);
        int LoadedAmmo(FItemInstanceId instanceId) const;
        const FInventoryContainer* FindContainer(FContainerId containerId) const;

        int ContainerUsed(FContainerId containerId) const;
        int UsedCapacity() const;
        int TotalCapacity() const;
        int FreeCapacity() const { return TotalCapacity() - UsedCapacity(); }

        int CountOf(const std::string& id) const;
        int Consume(const std::string& id, int count);

        bool IsClothingEquipped(const std::string& id) const;
        void SetClothingEquipped(const std::string& id, bool equipped);
        const std::vector<std::string>& EquippedClothing() const { return equippedClothing_; }

    private:
        static int InstanceVolume(const FItemInstance& instance);
        FContainerId FindContainerWithSpace(const FItemDef& definition, int count,
                                            FContainerId excluded = 0) const;
        void RebuildLegacyItems();
        void RemoveEmptyInstances();

        std::vector<FItemInstance> instances_;
        std::vector<FInventoryContainer> containers_;
        std::vector<FItemStack> legacyItems_;
        std::vector<std::string> equippedClothing_;
        FItemInstanceId nextInstanceId_ = 1;
        FContainerId nextContainerId_ = 1;
    };
}
