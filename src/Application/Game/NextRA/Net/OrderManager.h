#pragma once

#include "Net/Order.h"

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NextRA::Net
{
    class FOrderManager
    {
    public:
        void SetLockstepConfig(uint8_t playerCount, uint32_t orderLatency);
        void SubmitLocalOrder(FOrder order);
        void SubmitHeartbeat(uint8_t playerId, uint32_t issueTick);
        void ReceiveOrders(uint8_t playerId, uint32_t execTick, std::vector<FOrder> orders);
        bool CanAdvance(uint32_t tick) const;
        std::vector<FOrder> ConsumeExecOrders(uint32_t tick);
        size_t PendingOrderCount() const;
        void Clear();
        uint32_t OrderLatency() const { return orderLatency_; }

    private:
        struct FTickBucket
        {
            std::map<uint8_t, std::vector<FOrder>> ordersByPlayer;
            std::unordered_set<uint8_t> arrivedPlayers;
        };

        void ReceivePlayerBucket(uint8_t playerId, uint32_t execTick, std::vector<FOrder> orders);

        std::unordered_map<uint32_t, FTickBucket> ordersByTick_;
        uint8_t playerCount_ = 1;
        uint32_t orderLatency_ = 0;
    };
}
