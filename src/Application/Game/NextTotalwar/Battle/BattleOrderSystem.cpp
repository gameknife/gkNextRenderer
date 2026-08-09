#include "Battle/BattleOrderSystem.h"

#include <algorithm>

namespace NextTotalwar
{
    void FBattleOrderSystem::Reset()
    {
        pending_.clear();
        nextSequence_ = 1;
        acceptedCount_ = 0;
        rejectedCount_ = 0;
        lastRejection_.clear();
    }

    FOrderSubmission FBattleOrderSystem::Submit(FBattleOrder order,
                                                 const std::vector<FRegiment>& regiments)
    {
        FOrderSubmission result;
        const auto reject = [this, &result](const char* reason)
        {
            ++rejectedCount_;
            lastRejection_ = reason;
            result.reason = reason;
            return result;
        };
        if (order.regimentId < 0 || static_cast<size_t>(order.regimentId) >= regiments.size())
            return reject("invalid regiment");
        const FRegiment& regiment = regiments[static_cast<size_t>(order.regimentId)];
        if (regiment.id != order.regimentId || regiment.faction != order.issuerFaction)
            return reject("issuer does not own regiment");
        if (!IsRegimentSelectable(regiment) || regiment.moraleState == EMoraleState::Routing)
            return reject("regiment cannot accept orders");
        if ((order.type == EBattleOrderType::Attack || order.type == EBattleOrderType::Charge) &&
            (order.targetRegimentId < 0 ||
             static_cast<size_t>(order.targetRegimentId) >= regiments.size() ||
             regiments[static_cast<size_t>(order.targetRegimentId)].faction == order.issuerFaction ||
             !IsRegimentSelectable(regiments[static_cast<size_t>(order.targetRegimentId)])))
        {
            return reject("invalid enemy target");
        }
        if (order.type == EBattleOrderType::SetFormation && (order.ranks < 4 || order.ranks > 32))
            return reject("formation ranks out of range");

        order.sequence = nextSequence_++;
        pending_.push_back(order);
        ++acceptedCount_;
        result.accepted = true;
        result.sequence = order.sequence;
        return result;
    }

    size_t FBattleOrderSystem::ExecuteReady(uint64_t currentTick, const FExecutor& executor)
    {
        std::stable_sort(pending_.begin(), pending_.end(), [](const FBattleOrder& a, const FBattleOrder& b)
        {
            if (a.issuedTick != b.issuedTick) return a.issuedTick < b.issuedTick;
            return a.sequence < b.sequence;
        });
        size_t count = 0;
        auto firstFuture = pending_.begin();
        while (firstFuture != pending_.end() && firstFuture->issuedTick <= currentTick)
        {
            executor(*firstFuture);
            ++firstFuture;
            ++count;
        }
        pending_.erase(pending_.begin(), firstFuture);
        return count;
    }

    const char* FBattleOrderSystem::TypeName(EBattleOrderType type)
    {
        switch (type)
        {
        case EBattleOrderType::Move: return "Move";
        case EBattleOrderType::Attack: return "Attack";
        case EBattleOrderType::Charge: return "Charge";
        case EBattleOrderType::Halt: return "Halt";
        case EBattleOrderType::Withdraw: return "Withdraw";
        case EBattleOrderType::SetFormation: return "Formation";
        }
        return "Unknown";
    }
}
