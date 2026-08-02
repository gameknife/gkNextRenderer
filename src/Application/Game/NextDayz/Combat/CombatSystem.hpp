#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz
{
    struct FHitProxy
    {
        FZombieHandle zombie{};
        EHitZone zone = EHitZone::Torso;
    };

    class FHitProxyRegistry
    {
    public:
        void Register(uint32_t instanceId, FZombieHandle zombie, EHitZone zone);
        void Unregister(FZombieHandle zombie);
        void Clear();
        std::optional<FHitProxy> Resolve(uint32_t instanceId) const;

    private:
        std::unordered_map<uint32_t, FHitProxy> proxies_;
    };

    class CombatSystem
    {
    public:
        explicit CombatSystem(ZombieSystem& zombies) : zombies_(zombies) {}

        FHitProxyRegistry& HitProxies() { return hitProxies_; }
        bool ProcessHit(const FWeaponHitEvent& event, float maximumDistance, float minimumDamageScale = 0.55f);
        bool ProcessMelee(uint64_t attackSequence, FZombieHandle target, float damage, EHitZone zone);
        void Reset();

        FZombieHandle LastHitZombie() const { return lastHitZombie_; }

    private:
        ZombieSystem& zombies_;
        FHitProxyRegistry hitProxies_;
        FZombieHandle lastHitZombie_{};
        uint64_t lastMeleeSequence_ = 0;
        std::unordered_map<uint64_t, std::unordered_set<uint64_t>> pelletHits_;
    };
}
