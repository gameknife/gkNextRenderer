#include "Net/OrderManager.h"

#include <algorithm>
#include <iterator>

namespace NextRA::Net
{
    void FOrderManager::SetLockstepConfig(uint8_t playerCount, uint32_t orderLatency)
    {
        playerCount_ = std::max<uint8_t>(1, playerCount);
        orderLatency_ = orderLatency;
    }

    void FOrderManager::SubmitLocalOrder(FOrder order)
    {
        const uint8_t playerId = order.playerId;
        order.issueTick += orderLatency_;
        const uint32_t execTick = order.issueTick;
        std::vector<FOrder> orders;
        orders.push_back(std::move(order));
        ReceivePlayerBucket(playerId, execTick, std::move(orders));
    }

    void FOrderManager::SubmitHeartbeat(uint8_t playerId, uint32_t issueTick)
    {
        ReceivePlayerBucket(playerId, issueTick + orderLatency_, {});
    }

    void FOrderManager::ReceiveOrders(uint8_t playerId, uint32_t execTick, std::vector<FOrder> orders)
    {
        for (FOrder& order : orders)
        {
            order.issueTick = execTick;
        }
        ReceivePlayerBucket(playerId, execTick, std::move(orders));
    }

    bool FOrderManager::CanAdvance(uint32_t tick) const
    {
        const auto it = ordersByTick_.find(tick);
        if (it == ordersByTick_.end())
        {
            return false;
        }

        for (uint8_t playerId = 0; playerId < playerCount_; ++playerId)
        {
            if (!it->second.arrivedPlayers.contains(playerId))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<FOrder> FOrderManager::ConsumeExecOrders(uint32_t tick)
    {
        if (!CanAdvance(tick))
        {
            return {};
        }

        auto it = ordersByTick_.find(tick);
        std::vector<FOrder> orders;
        for (auto& [playerId, playerOrders] : it->second.ordersByPlayer)
        {
            (void)playerId;
            orders.insert(orders.end(),
                          std::make_move_iterator(playerOrders.begin()),
                          std::make_move_iterator(playerOrders.end()));
        }
        ordersByTick_.erase(it);
        return orders;
    }

    size_t FOrderManager::PendingOrderCount() const
    {
        size_t count = 0;
        for (const auto& [tick, bucket] : ordersByTick_)
        {
            (void)tick;
            for (const auto& [playerId, orders] : bucket.ordersByPlayer)
            {
                (void)playerId;
                count += orders.size();
            }
        }
        return count;
    }

    void FOrderManager::Clear()
    {
        ordersByTick_.clear();
    }

    void FOrderManager::ReceivePlayerBucket(uint8_t playerId, uint32_t execTick, std::vector<FOrder> orders)
    {
        FTickBucket& bucket = ordersByTick_[execTick];
        bucket.arrivedPlayers.insert(playerId);
        std::vector<FOrder>& playerOrders = bucket.ordersByPlayer[playerId];
        playerOrders.insert(playerOrders.end(),
                            std::make_move_iterator(orders.begin()),
                            std::make_move_iterator(orders.end()));
    }
}
