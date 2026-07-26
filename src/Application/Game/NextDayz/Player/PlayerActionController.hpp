#pragma once

#include <optional>

#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"

namespace NextDayz
{
    class PlayerActionController
    {
    public:
        void Configure(const FActionConfig& config) { config_ = config; }
        bool BeginLoot(const FLootHandle& handle);
        void RequestCancel();
        void Update(float deltaSeconds);
        void Reset();

        bool IsActive() const { return action_ != EPlayerAction::None; }
        EPlayerAction Action() const { return action_; }
        float NormalizedTime() const;
        bool IsCommitted() const { return committed_; }

        std::optional<FLootHandle> ConsumeCommitRequest();
        std::optional<FLootHandle> ConsumeCancelRequest();

    private:
        FActionConfig config_{};
        EPlayerAction action_ = EPlayerAction::None;
        FLootHandle lootHandle_{};
        float elapsed_ = 0.0f;
        bool committed_ = false;
        bool cancelRequested_ = false;
        std::optional<FLootHandle> commitRequest_;
        std::optional<FLootHandle> cancelRequest_;
    };
}
