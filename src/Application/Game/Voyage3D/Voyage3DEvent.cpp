#include "Voyage3DEvent.hpp"

#include "Voyage3DGameInstance.hpp"

namespace
{
    const Voyage3D::FEventDef* PickWeightedEvent(const std::vector<Voyage3D::FEventDef>& events, std::mt19937& rng)
    {
        int totalWeight = 0;
        for (const Voyage3D::FEventDef& event : events)
        {
            totalWeight += std::max(0, event.weight);
        }
        if (totalWeight <= 0)
        {
            return nullptr;
        }

        std::uniform_int_distribution<int> dist(1, totalWeight);
        int roll = dist(rng);
        for (const Voyage3D::FEventDef& event : events)
        {
            roll -= std::max(0, event.weight);
            if (roll <= 0)
            {
                return &event;
            }
        }
        return nullptr;
    }
}

namespace Voyage3D
{
    void RollEvent(Voyage3DGameInstance& gameInstance)
    {
        const FEventDef* event = PickWeightedEvent(gameInstance.GetEventDefs(), gameInstance.GetRng());
        if (!event || event->effect == "none")
        {
            return;
        }

        if (event->effect == "combat")
        {
            gameInstance.BeginPirateEncounter(event->enemyShip);
            return;
        }

        if (event->effect == "intel")
        {
            const auto& ports = gameInstance.GetPorts();
            const auto& goods = gameInstance.GetGoodsDefs();
            if (ports.empty() || goods.empty())
            {
                return;
            }
            std::uniform_int_distribution<int> portDist(0, static_cast<int>(ports.size() - 1));
            std::uniform_int_distribution<int> goodDist(0, static_cast<int>(goods.size() - 1));
            const FPortRuntime& port = ports[static_cast<size_t>(portDist(gameInstance.GetRng()))];
            const FGoodsDef& good = goods[static_cast<size_t>(goodDist(gameInstance.GetRng()))];
            gameInstance.GrantIntelDiscount(port.def.id, good.id, -0.2f);
            const std::string text = fmt::format(fmt::runtime(U8Text(u8"漂流瓶情报：{} 的 {} 价格便宜！")), port.def.name, good.name);
            gameInstance.PushToast(text);
            gameInstance.AddEventLog(text);
            return;
        }

        if (event->effect == "treasure")
        {
            gameInstance.AddGold(event->rewardGold);
            const int added = gameInstance.AddCargo(event->rewardCargoId, event->rewardCargoQty);
            const FGoodsDef* good = gameInstance.FindGoodsDef(event->rewardCargoId);
            const std::string goodName = good ? good->name : event->rewardCargoId;
            const std::string text = fmt::format(fmt::runtime(U8Text(u8"发现沉船！获得 {} 金币 + {} {}")), event->rewardGold, added, goodName);
            gameInstance.PushToast(text);
            gameInstance.AddEventLog(text);
            return;
        }

        if (event->effect == "storm")
        {
            gameInstance.DamagePlayer(event->hpDamage);
            gameInstance.ApplyStormDebuff(static_cast<float>(event->speedDebuffMs));
            const std::string text = fmt::format(fmt::runtime(U8Text(u8"暴风雨！HP -{} 速度下降")), event->hpDamage);
            gameInstance.PushToast(text);
            gameInstance.AddEventLog(text);
        }
    }
}
