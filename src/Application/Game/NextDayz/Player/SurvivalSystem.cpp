#include "Engine/Common/CoreMinimal.hpp"

#include "SurvivalSystem.hpp"

namespace NextDayz
{
    void SurvivalSystem::Reset()
    {
        state_ = {};
        state_.health = config_.StartingHealth;
        state_.hunger = config_.StartingHunger;
        state_.hydration = config_.StartingHydration;
        state_.lifeState = EPlayerLifeState::Alive;
        damageProtectionRemaining_ = 0.0f;
        ClampState();
    }

    void SurvivalSystem::Update(float simulationDeltaSeconds, bool sprinting)
    {
        const float deltaSeconds = std::max(simulationDeltaSeconds, 0.0f);
        damageProtectionRemaining_ = std::max(0.0f, damageProtectionRemaining_ - deltaSeconds);
        if (!IsAlive() || deltaSeconds <= 0.0f)
        {
            return;
        }
        const float hungerMultiplier = sprinting ? config_.SprintHungerMultiplier : 1.0f;
        const float hydrationMultiplier = sprinting ? config_.SprintHydrationMultiplier : 1.0f;
        state_.hunger -= config_.HungerDrainPerSecond * hungerMultiplier * deltaSeconds;
        state_.hydration -= config_.HydrationDrainPerSecond * hydrationMultiplier * deltaSeconds;
        ClampState();

        float damage = 0.0f;
        if (state_.hunger <= 0.0f)
        {
            damage += config_.StarvationDamagePerSecond * deltaSeconds;
        }
        if (state_.hydration <= 0.0f)
        {
            damage += config_.DehydrationDamagePerSecond * deltaSeconds;
        }
        if (damage > 0.0f)
        {
            ApplyDamage(damage, state_.hydration <= 0.0f ? "dehydration" : "starvation", false);
        }
    }

    bool SurvivalSystem::ApplyDamage(float amount, std::string_view source, bool respectProtection)
    {
        if (!IsAlive() || amount <= 0.0f || (respectProtection && damageProtectionRemaining_ > 0.0f))
        {
            return false;
        }
        state_.health -= amount;
        state_.lastDamageSource = source;
        ++state_.damageSequence;
        if (respectProtection)
        {
            damageProtectionRemaining_ = config_.DamageProtectionSeconds;
        }
        ClampState();
        if (state_.health <= 0.0f)
        {
            state_.lifeState = EPlayerLifeState::Dead;
        }
        return true;
    }

    bool SurvivalSystem::TryUseItem(Inventory& inventory, FItemInstanceId instanceId)
    {
        if (!IsAlive())
        {
            return false;
        }
        const FItemInstance* instance = inventory.FindInstance(instanceId);
        const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
        if (!definition || definition->kind != EItemKind::Consumable)
        {
            return false;
        }
        const float hunger = state_.hunger + definition->hungerDelta;
        const float hydration = state_.hydration + definition->hydrationDelta;
        const float health = state_.health + definition->healthDelta;
        if (!inventory.TryConsumeInstance(instanceId))
        {
            return false;
        }
        state_.hunger = hunger;
        state_.hydration = hydration;
        state_.health = health;
        ClampState();
        return true;
    }

    void SurvivalSystem::DrinkFromWell()
    {
        if (IsAlive())
        {
            state_.hydration += 55.0f;
            ClampState();
        }
    }

    bool SurvivalSystem::FillBottle(Inventory& inventory, FItemInstanceId emptyBottleId)
    {
        const FItemInstance* instance = inventory.FindInstance(emptyBottleId);
        if (!instance || instance->defId != "water_bottle_empty")
        {
            return false;
        }
        const FInventoryAddRequest bottle{"water_bottle", "Water Bottle", EItemKind::Consumable, 1};
        Inventory transaction = inventory;
        if (!transaction.TryConsumeInstance(emptyBottleId) || !transaction.TryAddBatch(std::span(&bottle, 1)))
        {
            return false;
        }
        inventory = std::move(transaction);
        return true;
    }

    void SurvivalSystem::SetNeeds(float health, float hunger, float hydration)
    {
        state_.health = health;
        state_.hunger = hunger;
        state_.hydration = hydration;
        state_.lifeState = health > 0.0f ? EPlayerLifeState::Alive : EPlayerLifeState::Dead;
        ClampState();
    }

    void SurvivalSystem::ClampState()
    {
        state_.health = glm::clamp(state_.health, 0.0f, 100.0f);
        state_.hunger = glm::clamp(state_.hunger, 0.0f, 100.0f);
        state_.hydration = glm::clamp(state_.hydration, 0.0f, 100.0f);
    }
}
