#include "PlayerActionController.hpp"

#include <algorithm>

namespace NextDayz
{
    bool PlayerActionController::BeginLoot(const FLootHandle& handle)
    {
        if (IsActive() || !handle.IsValid())
        {
            return false;
        }
        action_ = EPlayerAction::LootGround;
        lootHandle_ = handle;
        elapsed_ = 0.0f;
        committed_ = false;
        cancelRequested_ = false;
        commitRequest_.reset();
        cancelRequest_.reset();
        return true;
    }

    bool PlayerActionController::BeginUse(EPlayerAction action, FItemInstanceId instanceId)
    {
        if (IsActive() || action == EPlayerAction::None || action == EPlayerAction::LootGround)
        {
            return false;
        }
        if ((action == EPlayerAction::Eat || action == EPlayerAction::Drink || action == EPlayerAction::Heal ||
             action == EPlayerAction::FillBottle) && instanceId == 0)
        {
            return false;
        }
        action_ = action;
        itemInstanceId_ = instanceId;
        elapsed_ = 0.0f;
        committed_ = false;
        cancelRequested_ = false;
        commitRequest_.reset();
        cancelRequest_.reset();
        itemCommitRequest_.reset();
        return true;
    }

    void PlayerActionController::RequestCancel()
    {
        if (IsActive() && !committed_)
        {
            cancelRequested_ = true;
        }
    }

    void PlayerActionController::Update(float deltaSeconds)
    {
        if (!IsActive())
        {
            return;
        }
        if (cancelRequested_ && !committed_)
        {
            if (action_ == EPlayerAction::LootGround)
            {
                cancelRequest_ = lootHandle_;
            }
            action_ = EPlayerAction::None;
            cancelRequested_ = false;
            return;
        }

        elapsed_ += std::max(deltaSeconds, 0.0f);
        const float commitTime = action_ == EPlayerAction::LootGround
            ? config_.LootCommitNormalizedTime : config_.UseCommitNormalizedTime;
        if (!committed_ && NormalizedTime() >= commitTime)
        {
            committed_ = true;
            if (action_ == EPlayerAction::LootGround)
            {
                commitRequest_ = lootHandle_;
            }
            else
            {
                itemCommitRequest_ = FItemActionCommit{action_, itemInstanceId_};
            }
        }
        const float duration = action_ == EPlayerAction::LootGround ? config_.LootDurationSeconds
            : (action_ == EPlayerAction::DrinkFromWell || action_ == EPlayerAction::FillBottle)
                ? config_.WellDurationSeconds : config_.UseDurationSeconds;
        if (elapsed_ >= std::max(duration, 0.01f))
        {
            action_ = EPlayerAction::None;
        }
    }

    void PlayerActionController::Reset()
    {
        action_ = EPlayerAction::None;
        lootHandle_ = {};
        itemInstanceId_ = 0;
        elapsed_ = 0.0f;
        committed_ = false;
        cancelRequested_ = false;
        commitRequest_.reset();
        cancelRequest_.reset();
        itemCommitRequest_.reset();
    }

    float PlayerActionController::NormalizedTime() const
    {
        const float duration = action_ == EPlayerAction::LootGround ? config_.LootDurationSeconds
            : (action_ == EPlayerAction::DrinkFromWell || action_ == EPlayerAction::FillBottle)
                ? config_.WellDurationSeconds : config_.UseDurationSeconds;
        return glm::clamp(elapsed_ / std::max(duration, 0.01f), 0.0f, 1.0f);
    }

    std::optional<FLootHandle> PlayerActionController::ConsumeCommitRequest()
    {
        std::optional<FLootHandle> request;
        request.swap(commitRequest_);
        return request;
    }

    std::optional<FLootHandle> PlayerActionController::ConsumeCancelRequest()
    {
        std::optional<FLootHandle> request;
        request.swap(cancelRequest_);
        return request;
    }

    std::optional<FItemActionCommit> PlayerActionController::ConsumeItemCommitRequest()
    {
        std::optional<FItemActionCommit> request;
        request.swap(itemCommitRequest_);
        return request;
    }
}
