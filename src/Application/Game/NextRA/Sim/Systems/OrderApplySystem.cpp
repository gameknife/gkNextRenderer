#include "Sim/Systems/OrderApplySystem.h"

namespace NextRA::Sim
{
    void ApplyOrders(FSimWorld& world, const FPathfindGrid& grid, std::span<const Net::FOrder> orders)
    {
        for (const Net::FOrder& order : orders)
        {
            for (FActorId actor : order.actorIds)
            {
                const FOwner* owner = world.TryGetOwner(actor);
                if (!owner || owner->playerId != order.playerId)
                {
                    continue;
                }

                switch (order.type)
                {
                case Net::EOrderType::Move:
                    world.IssueMove(actor, order.targetPos, grid);
                    break;
                case Net::EOrderType::AttackMove:
                    world.IssueAttackMove(actor, order.targetPos, grid);
                    break;
                case Net::EOrderType::Attack:
                    if (const FSimTransform* targetTransform = world.TryGetTransform(order.targetActor))
                    {
                        world.IssueMove(actor, targetTransform->pos, grid);
                    }
                    world.IssueAttack(actor, order.targetActor);
                    break;
                case Net::EOrderType::Produce:
                    world.IssueProduce(actor, order.produceTypeId);
                    break;
                }
            }
        }
    }
}
