#include "Application/Game/NextRA/Sim/Fixed.h"
#include "Application/Game/NextRA/Net/Order.h"
#include "Application/Game/NextRA/Net/LoopbackTransport.h"
#include "Application/Game/NextRA/Net/OrderManager.h"
#include "Application/Game/NextRA/Net/Replay.h"
#include "Application/Game/NextRA/Sim/PathfindGrid.h"
#include "Application/Game/NextRA/Sim/SimWorld.h"
#include "Application/Game/NextRA/Sim/SyncHash.h"
#include "Application/Game/NextRA/Sim/Systems/OrderApplySystem.h"
#include "Application/Game/NextRA/Sim/WMath.h"
#include "Application/Game/NextRA/NextRAConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

namespace
{
    struct FNextRATestFixture
    {
        NextRA::Sim::FSimWorld world;
        NextRA::Sim::FPathfindGrid grid{32, 32, NextRA::Sim::CPos{-16, -16}};
        NextRA::Sim::FActorId playerBarracks = static_cast<NextRA::Sim::FActorId>(-1);
        NextRA::Sim::FActorId playerInfantry = static_cast<NextRA::Sim::FActorId>(-1);
        NextRA::Sim::FActorId enemyBase = static_cast<NextRA::Sim::FActorId>(-1);
        NextRA::Sim::FActorId enemyTank = static_cast<NextRA::Sim::FActorId>(-1);
    };

    FNextRATestFixture CreateNextRALockstepFixture(uint64_t seed = 1234)
    {
        using namespace NextRA::Sim;

        FNextRATestFixture fixture;
        fixture.world.SetRandomSeed(seed);
        fixture.world.SpawnBuilding(
            0,
            NextRA::baseTypeId,
            WPos::FromCells(-6, -5),
            NextRA::BaseMaxHp(),
            true,
            false,
            WPos::FromCells(-5, -5));
        fixture.playerBarracks = fixture.world.SpawnBuilding(
            0,
            NextRA::barracksTypeId,
            WPos::FromCells(-5, -3),
            NextRA::BarracksMaxHp(),
            false,
            true,
            WPos::FromCells(-3, -3));
        fixture.playerInfantry = fixture.world.SpawnMobile(
            0,
            NextRA::infantryTypeId,
            WPos::FromCells(-2, 0),
            WPos::FromCells(-2, 0),
            NextRA::UnitSpeedPerTick(NextRA::infantryTypeId),
            false);
        fixture.enemyBase = fixture.world.SpawnBuilding(
            1,
            NextRA::baseTypeId,
            WPos::FromCells(5, 0),
            NextRA::BaseMaxHp(),
            true,
            false,
            WPos::FromCells(4, 0));
        fixture.enemyTank = fixture.world.SpawnMobile(
            1,
            NextRA::tankTypeId,
            WPos::FromCells(1, 0),
            WPos::FromCells(1, 0),
            NextRA::UnitSpeedPerTick(NextRA::tankTypeId),
            false);
        return fixture;
    }

    std::vector<NextRA::Net::FOrder> CreateM5OrdersForTick(const FNextRATestFixture& fixture, uint32_t tick)
    {
        using namespace NextRA::Sim;

        std::vector<NextRA::Net::FOrder> orders;
        if (tick == 0)
        {
            NextRA::Net::FOrder produce;
            produce.type = NextRA::Net::EOrderType::Produce;
            produce.playerId = 0;
            produce.issueTick = tick;
            produce.actorIds = {fixture.playerBarracks};
            produce.produceTypeId = NextRA::infantryTypeId;
            orders.push_back(produce);

            NextRA::Net::FOrder attack;
            attack.type = NextRA::Net::EOrderType::Attack;
            attack.playerId = 0;
            attack.issueTick = tick;
            attack.actorIds = {fixture.playerInfantry};
            attack.targetActor = fixture.enemyTank;
            orders.push_back(attack);
        }
        if (tick == 40)
        {
            NextRA::Net::FOrder counterMove;
            counterMove.type = NextRA::Net::EOrderType::AttackMove;
            counterMove.playerId = 1;
            counterMove.issueTick = tick;
            counterMove.actorIds = {fixture.enemyTank};
            counterMove.targetPos = WPos::FromCells(-6, -5);
            orders.push_back(counterMove);
        }
        return orders;
    }

    void DrainLoopbackIntoManager(NextRA::Net::FLoopbackTransport& transport,
                                  NextRA::Net::FOrderManager& manager,
                                  uint8_t playerId,
                                  uint32_t networkTick)
    {
        for (const NextRA::Net::FNetPacket& packet : transport.Drain(playerId, networkTick))
        {
            if (packet.payload.empty())
            {
                manager.ReceiveOrders(packet.fromPlayer, packet.execTick, {});
                continue;
            }

            std::optional<NextRA::Net::FOrder> order = NextRA::Net::DeserializeOrder(packet.payload);
            REQUIRE(order.has_value());
            manager.ReceiveOrders(packet.fromPlayer, packet.execTick, {*order});
        }
    }
}

TEST_CASE("NextRA fixed arithmetic is deterministic", "[Unit][NextRA]")
{
    using NextRA::Sim::FFixed;

    const FFixed one = FFixed::FromInt(1);
    const FFixed two = FFixed::FromInt(2);
    const FFixed three = FFixed::FromInt(3);

    REQUIRE((one + two).raw == three.raw);
    REQUIRE((three - one).raw == two.raw);
    REQUIRE((two * three).raw == FFixed::FromInt(6).raw);
    REQUIRE((FFixed::FromInt(6) / three).raw == two.raw);
}

TEST_CASE("NextRA fixed sqrt and cell conversion", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    REQUIRE(Sqrt(FFixed::FromInt(9)).ToInt() == 3);

    const WPos pos = WPos::FromCells(-3, 7);
    const CPos cell = pos.ToCell();
    REQUIRE(cell.x == -3);
    REQUIRE(cell.z == 7);
}

TEST_CASE("NextRA angle lookup uses fixed deterministic cardinal values", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    REQUIRE(Sin(WAngle::FromRaw(0)).raw == 0);
    REQUIRE(Cos(WAngle::FromRaw(0)).raw == FFixed::oneRaw);
    REQUIRE(Sin(WAngle::FromRaw(angleUnits / 4)).raw == FFixed::oneRaw);
    REQUIRE(Cos(WAngle::FromRaw(angleUnits / 4)).raw == 0);
    REQUIRE(Sin(WAngle::FromRaw(angleUnits / 2)).raw == 0);
    REQUIRE(Cos(WAngle::FromRaw(angleUnits / 2)).raw == -FFixed::oneRaw);
    REQUIRE(Sin(WAngle::FromRaw(angleUnits * 3 / 4)).raw == -FFixed::oneRaw);
    REQUIRE(Cos(WAngle::FromRaw(angleUnits * 3 / 4)).raw == 0);
    REQUIRE(Sin(WAngle::FromRaw(-angleUnits / 4)).raw == -FFixed::oneRaw);
}

TEST_CASE("NextRA sim world moves actor with stable actor list", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    const FActorId actor = world.SpawnMobile(0, 1, WPos::FromCells(0, 0), WPos::FromCells(1, 0), FFixed::FromInt(256));

    for (uint32_t tick = 0; tick < 4; ++tick)
    {
        world.Step(tick);
    }

    const FSimTransform* transform = world.TryGetTransform(actor);
    REQUIRE(transform != nullptr);
    REQUIRE(transform->pos.x == WPos::FromCells(1, 0).x);
    REQUIRE(transform->pos.z == WPos::FromCells(1, 0).z);
    REQUIRE(world.Actors().front() == actor);
}

TEST_CASE("NextRA mobile actor can ping-pong for visible M1 validation", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    const FActorId actor =
        world.SpawnMobile(0, 1, WPos::FromCells(0, 0), WPos::FromCells(1, 0), FFixed::FromInt(512), true);

    world.Step(0);
    world.Step(1);
    const FSimTransform* atEnd = world.TryGetTransform(actor);
    REQUIRE(atEnd != nullptr);
    REQUIRE(atEnd->pos.x == WPos::FromCells(1, 0).x);

    world.Step(2);
    const FSimTransform* returning = world.TryGetTransform(actor);
    REQUIRE(returning != nullptr);
    REQUIRE(returning->pos.x < WPos::FromCells(1, 0).x);
}

TEST_CASE("NextRA sim world stores render links outside movement state", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    const FActorId actor = world.SpawnMobile(0, 1, WPos::FromCells(0, 0), WPos::FromCells(0, 0), FFixed::FromInt(1));
    world.SetRenderLink(actor, 1234);

    const FRenderLink* link = world.TryGetRenderLink(actor);
    REQUIRE(link != nullptr);
    REQUIRE(link->renderNodeId == 1234);
}

TEST_CASE("NextRA move order serialization round-trips", "[Unit][NextRA]")
{
    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Move;
    order.playerId = 1;
    order.issueTick = 42;
    order.actorIds = {7, 9};
    order.targetPos = NextRA::Sim::WPos::FromCells(3, -2);
    order.targetActor = 99;

    const std::vector<uint8_t> bytes = NextRA::Net::SerializeOrder(order);
    const std::optional<NextRA::Net::FOrder> decoded = NextRA::Net::DeserializeOrder(bytes);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->type == order.type);
    REQUIRE(decoded->playerId == order.playerId);
    REQUIRE(decoded->issueTick == order.issueTick);
    REQUIRE(decoded->actorIds == order.actorIds);
    REQUIRE(decoded->targetPos.x == order.targetPos.x);
    REQUIRE(decoded->targetPos.z == order.targetPos.z);
    REQUIRE(decoded->targetActor == order.targetActor);
    REQUIRE(decoded->produceTypeId == order.produceTypeId);
}

TEST_CASE("NextRA produce order serialization round-trips", "[Unit][NextRA]")
{
    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Produce;
    order.playerId = 0;
    order.issueTick = 17;
    order.actorIds = {3};
    order.produceTypeId = NextRA::tankTypeId;

    const std::vector<uint8_t> bytes = NextRA::Net::SerializeOrder(order);
    const std::optional<NextRA::Net::FOrder> decoded = NextRA::Net::DeserializeOrder(bytes);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->type == NextRA::Net::EOrderType::Produce);
    REQUIRE(decoded->actorIds == order.actorIds);
    REQUIRE(decoded->produceTypeId == NextRA::tankTypeId);
}

TEST_CASE("NextRA pathfind grid routes around blocked cells deterministically", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FPathfindGrid grid(7, 5, CPos{-3, -2});
    grid.SetBlocked(CPos{0, 0}, true);

    const std::vector<CPos> path = grid.FindPath(CPos{-2, 0}, CPos{2, 0});
    REQUIRE_FALSE(path.empty());
    REQUIRE(path.front() == CPos{-2, 0});
    REQUIRE(path.back() == CPos{2, 0});
    for (CPos cell : path)
    {
        REQUIRE_FALSE(cell == CPos{0, 0});
    }

    const std::vector<CPos> secondPath = grid.FindPath(CPos{-2, 0}, CPos{2, 0});
    REQUIRE(secondPath == path);
}

TEST_CASE("NextRA order manager applies move orders through path following", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    FPathfindGrid grid(16, 16, CPos{-8, -8});
    const FActorId actor = world.SpawnMobile(0, 1, WPos::FromCells(-2, 0), WPos::FromCells(-2, 0), FFixed::FromInt(512));

    NextRA::Net::FOrderManager orders;
    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Move;
    order.playerId = 0;
    order.issueTick = 0;
    order.actorIds = {actor};
    order.targetPos = WPos::FromCells(2, 0);
    orders.SubmitLocalOrder(order);

    std::vector<NextRA::Net::FOrder> execOrders = orders.ConsumeExecOrders(0);
    ApplyOrders(world, grid, execOrders);
    for (uint32_t tick = 0; tick < 8; ++tick)
    {
        world.Step(tick);
    }

    const FSimTransform* transform = world.TryGetTransform(actor);
    REQUIRE(transform != nullptr);
    REQUIRE(transform->pos.x == WPos::FromCells(2, 0).x);
    REQUIRE(transform->pos.z == WPos::FromCells(2, 0).z);
}

TEST_CASE("NextRA attack order damages and kills enemy", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    FPathfindGrid grid(16, 16, CPos{-8, -8});
    const FActorId attacker = world.SpawnMobile(0, 1, WPos::FromCells(-1, 0), WPos::FromCells(-1, 0), FFixed::FromInt(512));
    const FActorId target = world.SpawnMobile(1, 1, WPos::FromCells(1, 0), WPos::FromCells(1, 0), FFixed::FromInt(512));
    world.SetRenderLink(target, 9001);

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Attack;
    order.playerId = 0;
    order.issueTick = 0;
    order.actorIds = {attacker};
    order.targetActor = target;

    const std::array<NextRA::Net::FOrder, 1> orders{order};
    ApplyOrders(world, grid, orders);

    for (uint32_t tick = 0; tick < 80; ++tick)
    {
        world.Step(tick);
    }

    REQUIRE_FALSE(world.IsAlive(target));
    REQUIRE_FALSE(world.ConsumeDestroyedRenderNodeIds().empty());
}

TEST_CASE("NextRA attack move acquires enemies while moving", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    FPathfindGrid grid(16, 16, CPos{-8, -8});
    const FActorId attacker = world.SpawnMobile(0, 1, WPos::FromCells(-4, 0), WPos::FromCells(-4, 0), FFixed::FromInt(512));
    const FActorId target = world.SpawnMobile(1, 1, WPos::FromCells(0, 0), WPos::FromCells(0, 0), FFixed::FromInt(512));

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::AttackMove;
    order.playerId = 0;
    order.issueTick = 0;
    order.actorIds = {attacker};
    order.targetPos = WPos::FromCells(4, 0);

    const std::array<NextRA::Net::FOrder, 1> orders{order};
    ApplyOrders(world, grid, orders);

    for (uint32_t tick = 0; tick < 80; ++tick)
    {
        world.Step(tick);
    }

    REQUIRE_FALSE(world.IsAlive(target));
}

TEST_CASE("NextRA production queue spawns a new unit through orders", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    FPathfindGrid grid(16, 16, CPos{-8, -8});
    const FActorId barracks = world.SpawnBuilding(
        0,
        NextRA::barracksTypeId,
        WPos::FromCells(0, 0),
        NextRA::BarracksMaxHp(),
        false,
        true,
        WPos::FromCells(1, 0));

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Produce;
    order.playerId = 0;
    order.issueTick = 0;
    order.actorIds = {barracks};
    order.produceTypeId = NextRA::infantryTypeId;

    const std::array<NextRA::Net::FOrder, 1> orders{order};
    ApplyOrders(world, grid, orders);

    for (uint32_t tick = 0; tick < static_cast<uint32_t>(NextRA::ProductionBuildTicks(NextRA::infantryTypeId)); ++tick)
    {
        world.Step(tick);
    }

    const std::vector<FActorId> spawned = world.ConsumeSpawnedActorIds();
    REQUIRE(spawned.size() == 1);
    REQUIRE(world.IsAlive(spawned.front()));
    REQUIRE(world.TryGetMobile(spawned.front()) != nullptr);
    const FSimTransform* transform = world.TryGetTransform(spawned.front());
    REQUIRE(transform != nullptr);
    REQUIRE(transform->pos.x == WPos::FromCells(1, 0).x);
}

TEST_CASE("NextRA base death sets winner", "[Unit][NextRA]")
{
    using namespace NextRA::Sim;

    FSimWorld world;
    FPathfindGrid grid(16, 16, CPos{-8, -8});
    const FActorId attacker = world.SpawnMobile(0, 1, WPos::FromCells(-1, 0), WPos::FromCells(-1, 0), FFixed::FromInt(512));
    const FActorId enemyBase = world.SpawnBuilding(
        1,
        NextRA::baseTypeId,
        WPos::FromCells(1, 0),
        40,
        true,
        false,
        WPos::FromCells(1, 0));

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Attack;
    order.playerId = 0;
    order.issueTick = 0;
    order.actorIds = {attacker};
    order.targetActor = enemyBase;

    const std::array<NextRA::Net::FOrder, 1> orders{order};
    ApplyOrders(world, grid, orders);

    for (uint32_t tick = 0; tick < 80; ++tick)
    {
        world.Step(tick);
    }

    REQUIRE_FALSE(world.IsAlive(enemyBase));
    REQUIRE(world.WinnerPlayerId() == 0);
}

TEST_CASE("NextRA sync hash is stable for deterministic double run", "[Unit][NextRA]")
{
    FNextRATestFixture first = CreateNextRALockstepFixture();
    FNextRATestFixture second = CreateNextRALockstepFixture();

    for (uint32_t tick = 0; tick < 120; ++tick)
    {
        const std::vector<NextRA::Net::FOrder> firstOrders = CreateM5OrdersForTick(first, tick);
        const std::vector<NextRA::Net::FOrder> secondOrders = CreateM5OrdersForTick(second, tick);
        ApplyOrders(first.world, first.grid, firstOrders);
        ApplyOrders(second.world, second.grid, secondOrders);
        first.world.Step(tick);
        second.world.Step(tick);

        REQUIRE(NextRA::Sim::ComputeSyncHash(first.world) == NextRA::Sim::ComputeSyncHash(second.world));
    }
}

TEST_CASE("NextRA loopback transport gates missing peer packets", "[Unit][NextRA]")
{
    using namespace NextRA::Net;

    FNextRATestFixture first = CreateNextRALockstepFixture();
    FNextRATestFixture second = CreateNextRALockstepFixture();
    FOrderManager firstManager;
    FOrderManager secondManager;
    firstManager.SetLockstepConfig(2, 2);
    secondManager.SetLockstepConfig(2, 2);

    FOrder localOrder;
    localOrder.type = EOrderType::AttackMove;
    localOrder.playerId = 0;
    localOrder.issueTick = 0;
    localOrder.actorIds = {first.playerInfantry};
    localOrder.targetPos = NextRA::Sim::WPos::FromCells(2, 0);
    firstManager.SubmitLocalOrder(localOrder);
    secondManager.SubmitHeartbeat(1, 0);

    FLoopbackTransport transport;
    transport.SetDelayTicks(1);
    transport.SetDropEveryNthPacket(2);
    transport.Send(0, 1, 2, SerializeOrder(localOrder));
    transport.Send(1, 0, 2, {});

    DrainLoopbackIntoManager(transport, firstManager, 0, 3);
    DrainLoopbackIntoManager(transport, secondManager, 1, 3);
    REQUIRE_FALSE(firstManager.CanAdvance(2));
    REQUIRE(secondManager.CanAdvance(2));
    REQUIRE(transport.DroppedPacketCount() == 1);

    transport.SetDropEveryNthPacket(0);
    transport.Send(1, 0, 2, {});
    DrainLoopbackIntoManager(transport, firstManager, 0, 3);
    REQUIRE(firstManager.CanAdvance(2));

    const std::vector<FOrder> firstOrders = firstManager.ConsumeExecOrders(2);
    const std::vector<FOrder> secondOrders = secondManager.ConsumeExecOrders(2);
    ApplyOrders(first.world, first.grid, firstOrders);
    ApplyOrders(second.world, second.grid, secondOrders);
    first.world.Step(2);
    second.world.Step(2);

    REQUIRE(NextRA::Sim::ComputeSyncHash(first.world) == NextRA::Sim::ComputeSyncHash(second.world));

    FLoopbackTransport reorderTransport;
    reorderTransport.SetDelayTicks(1);
    reorderTransport.SetReorderEveryNthPacket(1, 3);
    reorderTransport.Send(0, 1, 10, {});
    REQUIRE(reorderTransport.Drain(1, 11).empty());
    REQUIRE(reorderTransport.Drain(1, 14).size() == 1);
}

TEST_CASE("NextRA replay replays recorded sync hash sequence", "[Unit][NextRA]")
{
    constexpr uint64_t seed = 98765;
    FNextRATestFixture original = CreateNextRALockstepFixture(seed);
    NextRA::Net::FReplay replay;
    replay.seed = seed;
    replay.latency = 0;

    for (uint32_t tick = 0; tick < 120; ++tick)
    {
        std::vector<NextRA::Net::FOrder> orders = CreateM5OrdersForTick(original, tick);
        ApplyOrders(original.world, original.grid, orders);
        original.world.Step(tick);
        replay.ticks.push_back(NextRA::Net::FReplayTick{
            tick,
            std::move(orders),
            NextRA::Sim::ComputeSyncHash(original.world),
        });
    }

    const std::vector<uint8_t> replayBytes = NextRA::Net::SerializeReplay(replay);
    const std::optional<NextRA::Net::FReplay> decoded = NextRA::Net::DeserializeReplay(replayBytes);
    REQUIRE(decoded.has_value());

    FNextRATestFixture replayed = CreateNextRALockstepFixture(decoded->seed);
    for (const NextRA::Net::FReplayTick& tick : decoded->ticks)
    {
        ApplyOrders(replayed.world, replayed.grid, tick.orders);
        replayed.world.Step(tick.tick);
        REQUIRE(NextRA::Sim::ComputeSyncHash(replayed.world) == tick.syncHash);
    }
}
