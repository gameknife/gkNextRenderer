#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"

namespace NextDayz
{
    struct FZombieVisualSlot
    {
        FZombieHandle owner{};
        uint32_t bodyInstanceId = 0;
        uint32_t headInstanceId = 0;
        bool active = false;
    };

    // Fixed-capacity ownership table. Rendering code binds pre-created rig or
    // kinematic-capsule proxy IDs to these slots; Acquire never grows storage.
    class ZombieVisualPool
    {
    public:
        explicit ZombieVisualPool(size_t capacity = 32) : slots_(capacity) {}

        FZombieVisualSlot* Acquire(FZombieHandle owner);
        bool Release(FZombieHandle owner);
        void Reset();
        size_t Capacity() const { return slots_.size(); }
        int ActiveCount() const;

    private:
        std::vector<FZombieVisualSlot> slots_;
    };
}
