#pragma once

#include "NextTotalwarTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace NextTotalwar
{
    struct FBattleOrder
    {
        EBattleOrderType type = EBattleOrderType::Move;
        int issuerFaction = 0;
        int regimentId = -1;
        int targetRegimentId = -1;
        glm::vec3 targetPosition{};
        float targetFacing = 0.0f;
        int ranks = 0;
        uint64_t issuedTick = 0;
        uint64_t sequence = 0;
    };

    struct FOrderSubmission
    {
        bool accepted = false;
        uint64_t sequence = 0;
        std::string reason;
    };

    class FBattleOrderSystem
    {
    public:
        using FExecutor = std::function<void(const FBattleOrder&)>;

        void Reset();
        FOrderSubmission Submit(FBattleOrder order, const std::vector<FRegiment>& regiments);
        size_t ExecuteReady(uint64_t currentTick, const FExecutor& executor);

        [[nodiscard]] size_t PendingCount() const { return pending_.size(); }
        [[nodiscard]] uint64_t AcceptedCount() const { return acceptedCount_; }
        [[nodiscard]] uint64_t RejectedCount() const { return rejectedCount_; }
        [[nodiscard]] const std::string& LastRejection() const { return lastRejection_; }

        static const char* TypeName(EBattleOrderType type);

    private:
        std::vector<FBattleOrder> pending_;
        uint64_t nextSequence_ = 1;
        uint64_t acceptedCount_ = 0;
        uint64_t rejectedCount_ = 0;
        std::string lastRejection_;
    };
}
