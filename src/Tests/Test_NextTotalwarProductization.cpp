#include <catch2/catch_test_macros.hpp>

#include "AI/CommanderAI.h"
#include "Battle/BattleOrderSystem.h"
#include "Battle/BattleSession.h"
#include "Battle/MoraleSystem.h"
#include "Battle/RangedCombatSystem.h"
#include "Data/BattleData.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace
{
    NextTotalwar::FRegiment MakeProductRegiment(int id, int faction,
                                                 const NextTotalwar::FUnitDef* definition,
                                                 glm::vec3 anchor, int strength = 40)
    {
        NextTotalwar::FRegiment regiment;
        regiment.id = id;
        regiment.faction = faction;
        regiment.def = definition;
        regiment.anchor = anchor;
        regiment.facing = faction == 0 ? glm::half_pi<float>() : -glm::half_pi<float>();
        regiment.strength = strength;
        regiment.startStrength = strength;
        regiment.morale = 70.0f;
        regiment.ammo = definition ? definition->startingAmmo : 0;
        regiment.soldiers.resize(static_cast<size_t>(strength));
        for (int index = 0; index < strength; ++index)
        {
            auto& soldier = regiment.soldiers[static_cast<size_t>(index)];
            soldier.slotIndex = index;
            soldier.health = 100;
            soldier.position = anchor;
        }
        return regiment;
    }
}

TEST_CASE("NextTotalwar session has explicit product phases", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    const FUnitDef definition;
    std::vector<FRegiment> regiments{
        MakeProductRegiment(0, 0, &definition, {-10.0f, 0.0f, 0.0f}),
        MakeProductRegiment(1, 1, &definition, {10.0f, 0.0f, 0.0f}),
    };
    FBattleSession session;
    session.Reset(42);
    REQUIRE(session.Phase() == EBattlePhase::Briefing);
    REQUIRE(session.BeginDeployment());
    REQUIRE(session.BeginBattle());
    REQUIRE(session.TogglePause());
    REQUIRE(session.Phase() == EBattlePhase::Paused);
    REQUIRE(session.TogglePause());
    regiments[1].strength = 0;
    session.Tick(0.05f, regiments);
    CHECK(session.Phase() == EBattlePhase::Finished);
    CHECK(session.Result() == EBattleResult::Victory);

    regiments[1].strength = regiments[1].startStrength;
    regiments[1].moraleState = EMoraleState::Routing;
    session.Reset(43, true);
    session.Tick(0.05f, regiments);
    CHECK(session.Phase() == EBattlePhase::Finished);
    CHECK(session.Result() == EBattleResult::Victory);
}

TEST_CASE("NextTotalwar scenario and unit tuning load from product data", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    std::array<FUnitDef, 3> definitions{{
        {EUnitType::Spearman, "spearman", "Spearmen"},
        {EUnitType::Swordsman, "swordsman", "Swordsmen"},
        {EUnitType::Archer, "archer", "Archers"},
    }};
    const FBattleScenario scenario = LoadBattleProductData(definitions);
    CHECK(scenario.id == "greenfield");
    CHECK(scenario.playerFaction == 0);
    CHECK(scenario.aiFaction == 1);
    CHECK(scenario.regiments.size() == 24);
    CHECK(scenario.soldiersPerRegiment == 100);
    CHECK(definitions[2].canRangedAttack);
    CHECK(definitions[2].startingAmmo == 18);
    CHECK(definitions[2].rangedMinRange == 5.0f);
    CHECK(definitions[2].rangedRange == 55.0f);
}

TEST_CASE("NextTotalwar unified orders validate ownership and sequence", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    const FUnitDef definition;
    std::vector<FRegiment> regiments{
        MakeProductRegiment(0, 0, &definition, {-10.0f, 0.0f, 0.0f}),
        MakeProductRegiment(1, 1, &definition, {10.0f, 0.0f, 0.0f}),
    };
    FBattleOrderSystem orders;
    FBattleOrder move;
    move.regimentId = 0;
    move.issuerFaction = 0;
    move.issuedTick = 4;
    REQUIRE(orders.Submit(move, regiments).accepted);
    move.regimentId = 1;
    CHECK_FALSE(orders.Submit(move, regiments).accepted);
    size_t executed = 0;
    CHECK(orders.ExecuteReady(3, [&](const FBattleOrder&) { ++executed; }) == 0);
    CHECK(orders.ExecuteReady(4, [&](const FBattleOrder& order)
    {
        ++executed;
        CHECK(order.sequence == 1);
    }) == 1);
    CHECK(executed == 1);
}

TEST_CASE("NextTotalwar morale routes and can rally", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    const FUnitDef definition;
    std::vector<FRegiment> regiments{
        MakeProductRegiment(0, 0, &definition, {-10.0f, 0.0f, 0.0f}),
        MakeProductRegiment(1, 1, &definition, {10.0f, 0.0f, 0.0f}),
    };
    regiments[0].morale = 20.0f;
    FBattleState state;
    FMoraleSystem morale;
    morale.Tick(0.05f, regiments, state);
    REQUIRE(regiments[0].moraleState == EMoraleState::Routing);
    regiments[1].anchor = {180.0f, 0.0f, 0.0f};
    regiments[0].morale = 40.0f;
    for (int tick = 0; tick < 120; ++tick) morale.Tick(0.05f, regiments, state);
    CHECK(regiments[0].moraleState == EMoraleState::Rallying);
    CHECK(morale.RoutedCount() == 1);
    CHECK(morale.RalliedCount() == 1);
}

TEST_CASE("NextTotalwar archers use ammo and resolve deterministic volleys", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    FUnitDef archer;
    archer.type = EUnitType::Archer;
    archer.canRangedAttack = true;
    archer.rangedRange = 90.0f;
    archer.rangedMinRange = 8.0f;
    archer.volleyInterval = 1.0f;
    archer.startingAmmo = 12;
    archer.rangedAccuracy = 0.62f;
    archer.rangedDamage = 100.0f;
    FUnitDef infantry;
    const auto run = [&](uint64_t seed)
    {
        std::vector<FRegiment> regiments{
            MakeProductRegiment(0, 0, &archer, {-30.0f, 0.0f, 0.0f}, 50),
            MakeProductRegiment(1, 1, &infantry, {30.0f, 0.0f, 0.0f}, 50),
        };
        FBattleState state;
        FRangedCombatSystem ranged;
        ranged.Reset(seed);
        for (int tick = 0; tick < 100; ++tick)
        {
            ranged.Tick(0.05f, regiments, state);
            state.events.clear();
        }
        return std::pair{regiments[1].strength, regiments[0].ammo};
    };
    const auto first = run(1984);
    const auto second = run(1984);
    CHECK(first == second);
    CHECK(first.first < 50);
    CHECK(first.second < 12);
}

TEST_CASE("NextTotalwar ranged combat respects representative terrain occlusion", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    FUnitDef archer;
    archer.type = EUnitType::Archer;
    archer.canRangedAttack = true;
    archer.rangedRange = 90.0f;
    archer.rangedMinRange = 5.0f;
    archer.volleyInterval = 1.0f;
    archer.startingAmmo = 5;
    archer.rangedAccuracy = 1.0f;
    archer.rangedDamage = 100.0f;
    FUnitDef infantry;
    std::vector<FRegiment> regiments{
        MakeProductRegiment(0, 0, &archer, {-30.0f, 0.0f, 0.0f}, 20),
        MakeProductRegiment(1, 1, &infantry, {30.0f, 0.0f, 0.0f}, 20),
    };
    FBattleContext context;
    context.sampleGround = [](float x, float) { return std::abs(x) < 12.0f ? 30.0f : 0.0f; };
    FBattleState state;
    FRangedCombatSystem ranged;
    ranged.Reset(5);
    ranged.Tick(0.05f, regiments, state, {}, &context);
    CHECK(regiments[0].ammo == 5);
    CHECK(regiments[1].strength == 20);
    CHECK(ranged.VolleyCount() == 0);
}

TEST_CASE("NextTotalwar commander outputs deterministic AI orders", "[NextTotalwar][Product]")
{
    using namespace NextTotalwar;
    FUnitDef infantry;
    FUnitDef archer;
    archer.type = EUnitType::Archer;
    archer.canRangedAttack = true;
    std::vector<FRegiment> regiments{
        MakeProductRegiment(0, 0, &infantry, {-40.0f, 0.0f, 0.0f}),
        MakeProductRegiment(1, 1, &infantry, {40.0f, 0.0f, -10.0f}),
        MakeProductRegiment(2, 1, &archer, {50.0f, 0.0f, 10.0f}),
    };
    FCommanderAI first;
    FCommanderAI second;
    first.Reset(7, 1);
    second.Reset(7, 1);
    const auto a = first.Tick(1.0f, 20, 1.0f, regiments);
    const auto b = second.Tick(1.0f, 20, 1.0f, regiments);
    REQUIRE(a.size() == 2);
    REQUIRE(a.size() == b.size());
    for (size_t index = 0; index < a.size(); ++index)
    {
        CHECK(a[index].type == b[index].type);
        CHECK(a[index].regimentId == b[index].regimentId);
        CHECK(a[index].targetRegimentId == b[index].targetRegimentId);
        CHECK(a[index].targetPosition == b[index].targetPosition);
    }
    CHECK(a[1].type == EBattleOrderType::Attack);
}
