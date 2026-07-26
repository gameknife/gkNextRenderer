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
            cancelRequest_ = lootHandle_;
            action_ = EPlayerAction::None;
            cancelRequested_ = false;
            return;
        }

        elapsed_ += std::max(deltaSeconds, 0.0f);
        if (!committed_ && NormalizedTime() >= config_.LootCommitNormalizedTime)
        {
            committed_ = true;
            commitRequest_ = lootHandle_;
        }
        if (elapsed_ >= std::max(config_.LootDurationSeconds, 0.01f))
        {
            action_ = EPlayerAction::None;
        }
    }

    void PlayerActionController::Reset()
    {
        action_ = EPlayerAction::None;
        lootHandle_ = {};
        elapsed_ = 0.0f;
        committed_ = false;
        cancelRequested_ = false;
        commitRequest_.reset();
        cancelRequest_.reset();
    }

    float PlayerActionController::NormalizedTime() const
    {
        return glm::clamp(elapsed_ / std::max(config_.LootDurationSeconds, 0.01f), 0.0f, 1.0f);
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
}
