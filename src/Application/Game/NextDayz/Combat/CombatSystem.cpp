#include "Engine/Common/CoreMinimal.hpp"

#include "CombatSystem.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    void FHitProxyRegistry::Register(uint32_t instanceId, FZombieHandle zombie, EHitZone zone)
    {
        if (instanceId != 0 && zombie.IsValid())
        {
            proxies_[instanceId] = {zombie, zone};
        }
    }

    void FHitProxyRegistry::Unregister(FZombieHandle zombie)
    {
        std::erase_if(proxies_, [zombie](const auto& entry) { return entry.second.zombie == zombie; });
    }

    void FHitProxyRegistry::Clear()
    {
        proxies_.clear();
    }

    std::optional<FHitProxy> FHitProxyRegistry::Resolve(uint32_t instanceId) const
    {
        const auto it = proxies_.find(instanceId);
        return it == proxies_.end() ? std::nullopt : std::optional<FHitProxy>(it->second);
    }

    bool CombatSystem::ProcessHit(const FWeaponHitEvent& event, float maximumDistance, float minimumDamageScale)
    {
        const std::optional<FHitProxy> proxy = hitProxies_.Resolve(event.hitInstanceId);
        if (!proxy || !zombies_.Resolve(proxy->zombie))
        {
            return false;
        }
        const float distance = glm::distance(event.origin, event.hitPoint);
        const float distanceScale = glm::clamp(1.0f - distance / std::max(maximumDistance, 0.01f),
                                               glm::clamp(minimumDamageScale, 0.0f, 1.0f), 1.0f);
        if (!zombies_.ApplyDamage(proxy->zombie, event.baseDamage * distanceScale, proxy->zone))
        {
            return false;
        }
        lastHitZombie_ = proxy->zombie;
        return true;
    }

    bool CombatSystem::ProcessMelee(uint64_t attackSequence, FZombieHandle target, float damage, EHitZone zone)
    {
        if (attackSequence == 0 || attackSequence == lastMeleeSequence_)
        {
            return false;
        }
        lastMeleeSequence_ = attackSequence;
        if (!zombies_.ApplyDamage(target, damage, zone))
        {
            return false;
        }
        lastHitZombie_ = target;
        return true;
    }

    void CombatSystem::Reset()
    {
        hitProxies_.Clear();
        lastHitZombie_ = {};
        lastMeleeSequence_ = 0;
        pelletHits_.clear();
    }
}
