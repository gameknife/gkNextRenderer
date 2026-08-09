#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"

namespace NextDayz
{
    struct FSurvivalSnapshot
    {
        float health = 100.0f;
        float hunger = 100.0f;
        float hydration = 100.0f;
        EPlayerLifeState lifeState = EPlayerLifeState::Alive;
        uint64_t damageSequence = 0;
        std::string lastDamageSource;
    };

    class SurvivalSystem
    {
    public:
        void Configure(const FSurvivalConfig& config) { config_ = config; }
        void Reset();
        void Update(float simulationDeltaSeconds, bool sprinting);

        bool ApplyDamage(float amount, std::string_view source, bool respectProtection = true);
        bool TryUseItem(Inventory& inventory, FItemInstanceId instanceId);
        void DrinkFromWell();
        bool FillBottle(Inventory& inventory, FItemInstanceId emptyBottleId);

        const FSurvivalSnapshot& Snapshot() const { return state_; }
        bool IsAlive() const { return state_.lifeState == EPlayerLifeState::Alive; }

        // Deterministic validation hook used by pure tests and agent-only callers.
        void SetNeeds(float health, float hunger, float hydration);

    private:
        void ClampState();

        FSurvivalConfig config_{};
        FSurvivalSnapshot state_{};
        float damageProtectionRemaining_ = 0.0f;
    };
}
