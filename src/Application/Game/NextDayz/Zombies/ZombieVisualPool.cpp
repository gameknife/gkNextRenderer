#include "Engine/Common/CoreMinimal.hpp"

#include "ZombieVisualPool.hpp"

namespace NextDayz
{
    FZombieVisualSlot* ZombieVisualPool::Acquire(FZombieHandle owner)
    {
        if (!owner.IsValid())
        {
            return nullptr;
        }
        const auto existing = std::find_if(slots_.begin(), slots_.end(),
            [owner](const FZombieVisualSlot& slot) { return slot.active && slot.owner == owner; });
        if (existing != slots_.end())
        {
            return &*existing;
        }
        const auto free = std::find_if(slots_.begin(), slots_.end(),
            [](const FZombieVisualSlot& slot) { return !slot.active; });
        if (free == slots_.end())
        {
            return nullptr;
        }
        free->owner = owner;
        free->active = true;
        return &*free;
    }

    bool ZombieVisualPool::Release(FZombieHandle owner)
    {
        const auto slot = std::find_if(slots_.begin(), slots_.end(),
            [owner](const FZombieVisualSlot& candidate) { return candidate.active && candidate.owner == owner; });
        if (slot == slots_.end())
        {
            return false;
        }
        slot->owner = {};
        slot->active = false;
        return true;
    }

    void ZombieVisualPool::Reset()
    {
        for (FZombieVisualSlot& slot : slots_)
        {
            slot = {};
        }
    }

    int ZombieVisualPool::ActiveCount() const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(),
            [](const FZombieVisualSlot& slot) { return slot.active; }));
    }
}
