#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Battle/CombatGrid.h"
#include "Battle/CombatModel.h"
#include "Battle/CombatSystem.h"
#include "Battle/FormationLayout.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <tuple>
#include <vector>

namespace
{
    NextTotalwar::FUnitDef MakeUnitDef(NextTotalwar::EUnitType type)
    {
        NextTotalwar::FUnitDef definition;
        definition.type = type;
        definition.defaultRanks = 10;
        definition.fileSpacing = 1.1f;
        definition.rankSpacing = 1.3f;
        return definition;
    }

    NextTotalwar::FRegiment MakeRegiment(int id, int faction,
                                         const NextTotalwar::FUnitDef* definition,
                                         const glm::vec3& anchor, float facing, int count)
    {
        using namespace NextTotalwar;
        FRegiment regiment;
        regiment.id = id;
        regiment.faction = faction;
        regiment.def = definition;
        regiment.anchor = anchor;
        regiment.facing = facing;
        regiment.ranks = std::min(10, count);
        regiment.strength = count;
        regiment.startStrength = count;
        regiment.morale = CombatDef(definition->type).baseMorale;
        regiment.soldiers.resize(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index)
        {
            FSoldier& soldier = regiment.soldiers[index];
            soldier.slotIndex = index;
            soldier.position = Formation::SlotWorld(
                anchor, facing,
                Formation::SlotLocalOffset(index, count, regiment.ranks,
                                           definition->fileSpacing, definition->rankSpacing));
            soldier.yaw = facing;
            soldier.health = static_cast<int16_t>(CombatDef(definition->type).maxHealth);
            soldier.attackTimer =
                CombatDef(definition->type).attackInterval *
                (static_cast<float>((id * 67 + index * 23) % 101) / 101.0f);
        }
        return regiment;
    }

    std::vector<std::array<int, 2>> RunBattle(uint64_t seed)
    {
        using namespace NextTotalwar;
        static const FUnitDef blue = []
        {
            FUnitDef definition = MakeUnitDef(EUnitType::Swordsman);
            definition.fileSpacing = 0.05f;
            definition.rankSpacing = 0.05f;
            return definition;
        }();
        static const FUnitDef red = []
        {
            FUnitDef definition = MakeUnitDef(EUnitType::Spearman);
            definition.fileSpacing = 0.05f;
            definition.rankSpacing = 0.05f;
            return definition;
        }();
        std::vector<FRegiment> regiments;
        regiments.push_back(MakeRegiment(0, 0, &blue, {}, 0.0f, 100));
        regiments.push_back(MakeRegiment(1, 1, &red, {}, glm::pi<float>(), 100));
        for (size_t index = 0; index < regiments[1].soldiers.size(); ++index)
        {
            regiments[1].soldiers[index].position = regiments[0].soldiers[index].position;
        }

        FCombatTuning tuning;
        FCombatSystem system(seed);
        FBattleState state;
        std::vector<std::array<int, 2>> history;
        history.reserve(2000);
        for (int tick = 0; tick < 2000; ++tick)
        {
            system.Tick(0.05f, regiments, tuning, state);
            history.push_back({regiments[0].strength, regiments[1].strength});
            state.events.clear();
        }
        return history;
    }
}

TEST_CASE("NextTotalwar melee hit chance is monotonic and clamped",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    FCombatTuning tuning;
    CHECK(CombatModel::HitChance(-100, 100, tuning) == tuning.minHitChance);
    CHECK(CombatModel::HitChance(100, -100, tuning) == tuning.maxHitChance);
    CHECK(CombatModel::HitChance(12, 10, tuning) >
          CombatModel::HitChance(10, 10, tuning));
}

TEST_CASE("NextTotalwar destroyed or empty regiments cannot be selected",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    FRegiment regiment;
    regiment.strength = 1;
    regiment.state = ERegimentState::Idle;
    CHECK(IsRegimentSelectable(regiment));

    regiment.strength = 0;
    CHECK_FALSE(IsRegimentSelectable(regiment));

    regiment.strength = 1;
    regiment.state = ERegimentState::Destroyed;
    CHECK_FALSE(IsRegimentSelectable(regiment));
}

TEST_CASE("NextTotalwar attack arcs classify front flank and rear",
          "[NextTotalwar][Combat]")
{
    using NextTotalwar::CombatModel::EAttackArc;
    CHECK(NextTotalwar::CombatModel::ClassifyAttackArc(0.0f, {0.0f, 0.0f, 1.0f}) ==
          EAttackArc::Front);
    CHECK(NextTotalwar::CombatModel::ClassifyAttackArc(0.0f, {1.0f, 0.0f, 0.0f}) ==
          EAttackArc::Flank);
    CHECK(NextTotalwar::CombatModel::ClassifyAttackArc(0.0f, {0.0f, 0.0f, -1.0f}) ==
          EAttackArc::Rear);
}

TEST_CASE("NextTotalwar combat grid matches brute force radius query",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef blue = MakeUnitDef(EUnitType::Spearman);
    const FUnitDef red = MakeUnitDef(EUnitType::Swordsman);
    std::vector<FRegiment> regiments;
    regiments.push_back(MakeRegiment(0, 0, &blue, {-2.0f, 0.0f, 0.0f}, 0.0f, 20));
    regiments.push_back(MakeRegiment(1, 1, &red, {2.0f, 0.0f, 0.0f}, 0.0f, 20));
    regiments[0].engagedWith = {1};
    regiments[1].engagedWith = {0};

    FCombatGrid grid;
    grid.Build(regiments);
    std::vector<FCombatGridEntry> actual;
    grid.Query({0.0f, 0.0f, 0.0f}, 3.0f, actual);
    std::sort(actual.begin(), actual.end(), [](const auto& first, const auto& second)
    {
        return std::tie(first.regiment, first.soldier) <
               std::tie(second.regiment, second.soldier);
    });

    std::vector<FCombatGridEntry> expected;
    for (size_t regiment = 0; regiment < regiments.size(); ++regiment)
    {
        for (size_t soldier = 0; soldier < regiments[regiment].soldiers.size(); ++soldier)
        {
            const glm::vec3& position = regiments[regiment].soldiers[soldier].position;
            if (glm::length(glm::vec2(position.x, position.z)) <= 3.0f)
            {
                expected.push_back({
                    static_cast<int16_t>(regiment), static_cast<int16_t>(soldier)});
            }
        }
    }
    CHECK(actual == expected);
}

TEST_CASE("NextTotalwar damage reaches the death boundary exactly",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef attackerDef = MakeUnitDef(EUnitType::Swordsman);
    const FUnitDef targetDef = MakeUnitDef(EUnitType::Archer);
    std::vector<FRegiment> regiments;
    regiments.push_back(MakeRegiment(0, 0, &attackerDef, {}, 0.0f, 1));
    regiments.push_back(MakeRegiment(1, 1, &targetDef, {}, glm::pi<float>(), 1));
    regiments[0].soldiers[0].attackTimer = 0.0f;
    regiments[1].soldiers[0].attackTimer = 100.0f;
    regiments[1].soldiers[0].health =
        static_cast<int16_t>(CombatDef(EUnitType::Swordsman).damage);

    FCombatTuning tuning;
    tuning.minHitChance = 1.0f;
    tuning.maxHitChance = 1.0f;
    FCombatSystem system(17);
    FBattleState state;
    system.Tick(0.05f, regiments, tuning, state);

    CHECK(regiments[1].strength == 0);
    CHECK(regiments[1].soldiers[0].combatState == ESoldierState::Dying);
    CHECK(regiments[0].kills == 1);
    const auto hitEvent = std::find_if(
        state.events.begin(), state.events.end(), [](const FCombatEvent& event)
        {
            return event.type == ECombatEventType::Hit;
        });
    REQUIRE(hitEvent != state.events.end());
    CHECK(hitEvent->sourceRegiment == 0);
    CHECK(hitEvent->sourceSoldier == 0);
    const auto deathEvent = std::find_if(
        state.events.begin(), state.events.end(), [](const FCombatEvent& event)
        {
            return event.type == ECombatEventType::Death;
        });
    REQUIRE(deathEvent != state.events.end());
    CHECK(deathEvent->sourceRegiment == 0);
    CHECK(deathEvent->sourceSoldier == 0);
    CHECK(std::count_if(state.events.begin(), state.events.end(), [](const FCombatEvent& event)
    {
        return event.type == ECombatEventType::Death;
    }) == 1);
}

TEST_CASE("NextTotalwar separated front lines acquire targets before overlap",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef blue = MakeUnitDef(EUnitType::Spearman);
    const FUnitDef red = MakeUnitDef(EUnitType::Spearman);
    std::vector<FRegiment> regiments;
    regiments.push_back(
        MakeRegiment(0, 0, &blue, {-2.0f, 0.0f, 0.0f}, glm::half_pi<float>(), 1));
    regiments.push_back(
        MakeRegiment(1, 1, &red, {2.0f, 0.0f, 0.0f}, -glm::half_pi<float>(), 1));

    FCombatTuning tuning;
    REQUIRE(4.0f > tuning.engageMargin);
    REQUIRE(4.0f > tuning.searchRadius);
    REQUIRE(4.0f < tuning.searchRadius + tuning.maxBreakDistance);

    FCombatSystem system(1337);
    FBattleState state;
    system.Tick(0.05f, regiments, tuning, state);

    CHECK(regiments[0].state == ERegimentState::Engaged);
    CHECK(regiments[1].state == ERegimentState::Engaged);
    CHECK(regiments[0].soldiers[0].combatState == ESoldierState::Fighting);
    CHECK(regiments[1].soldiers[0].combatState == ESoldierState::Fighting);
    CHECK(regiments[0].soldiers[0].targetRegiment == 1);
    CHECK(regiments[1].soldiers[0].targetRegiment == 0);
    CHECK(regiments[0].strength == 1);
    CHECK(regiments[1].strength == 1);
}

TEST_CASE("NextTotalwar nearby regiments commit every living soldier to combat",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef blue = MakeUnitDef(EUnitType::Swordsman);
    const FUnitDef red = MakeUnitDef(EUnitType::Spearman);
    std::vector<FRegiment> regiments;
    regiments.push_back(
        MakeRegiment(0, 0, &blue, {-5.0f, 0.0f, 0.0f}, glm::half_pi<float>(), 100));
    regiments.push_back(
        MakeRegiment(1, 1, &red, {5.0f, 0.0f, 0.0f}, -glm::half_pi<float>(), 100));

    FCombatTuning tuning;
    REQUIRE(10.0f < tuning.regimentEngageDistance);
    FCombatSystem system(1337);
    FBattleState state;
    system.Tick(0.05f, regiments, tuning, state);

    CHECK(regiments[0].state == ERegimentState::Engaged);
    CHECK(regiments[1].state == ERegimentState::Engaged);
    for (const FRegiment& regiment : regiments)
    {
        CHECK(std::count_if(
                  regiment.soldiers.begin(), regiment.soldiers.end(),
                  [](const FSoldier& soldier)
                  {
                      return soldier.combatState == ESoldierState::Fighting &&
                             soldier.targetRegiment >= 0 &&
                             soldier.targetSoldier >= 0;
                  }) == 100);
        CHECK(regiment.strength == 100);
    }
}

TEST_CASE("NextTotalwar attackers reserve distinct slots around one target",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef blue = MakeUnitDef(EUnitType::Swordsman);
    const FUnitDef red = MakeUnitDef(EUnitType::Spearman);
    std::vector<FRegiment> regiments;
    regiments.push_back(
        MakeRegiment(0, 0, &blue, {-2.0f, 0.0f, 0.0f}, glm::half_pi<float>(), 3));
    regiments.push_back(
        MakeRegiment(1, 1, &red, {2.0f, 0.0f, 0.0f}, -glm::half_pi<float>(), 1));

    FCombatTuning tuning;
    tuning.maxAttackersPerTarget = 3;
    FCombatSystem system(1337);
    FBattleState state;
    system.Tick(0.05f, regiments, tuning, state);

    std::array<int, 3> slots{};
    for (size_t index = 0; index < slots.size(); ++index)
    {
        const FSoldier& soldier = regiments[0].soldiers[index];
        REQUIRE(soldier.targetRegiment == 1);
        REQUIRE(soldier.targetSoldier == 0);
        slots[index] = soldier.engagementSlot;
    }
    std::sort(slots.begin(), slots.end());
    CHECK(slots == std::array{-1, 0, 1});
}

TEST_CASE("NextTotalwar disengaging regiment is not pulled back into combat",
          "[NextTotalwar][Combat]")
{
    using namespace NextTotalwar;
    const FUnitDef blue = MakeUnitDef(EUnitType::Swordsman);
    const FUnitDef red = MakeUnitDef(EUnitType::Spearman);
    std::vector<FRegiment> regiments;
    regiments.push_back(
        MakeRegiment(0, 0, &blue, {-1.5f, 0.0f, 0.0f}, glm::half_pi<float>(), 1));
    regiments.push_back(
        MakeRegiment(1, 1, &red, {1.5f, 0.0f, 0.0f}, -glm::half_pi<float>(), 1));
    regiments[0].state = ERegimentState::Marching;
    regiments[0].disengaging = true;

    FCombatTuning tuning;
    tuning.regimentEngageDistance = 12.0f;
    FCombatSystem system(1337);
    FBattleState state;
    system.Tick(0.05f, regiments, tuning, state);

    CHECK(regiments[0].state == ERegimentState::Marching);
    CHECK(regiments[0].disengaging);
    CHECK(regiments[0].engagedWith.empty());
    CHECK(regiments[1].state == ERegimentState::Engaged);
    CHECK(regiments[1].engagedWith == std::vector<int16_t>{0});
    CHECK(regiments[0].soldiers[0].combatState == ESoldierState::Formation);
    CHECK(regiments[1].soldiers[0].combatState == ESoldierState::Fighting);
    CHECK(regiments[1].soldiers[0].targetRegiment == 0);

    regiments[0].anchor.x = -20.0f;
    regiments[0].soldiers[0].position.x = -20.0f;
    system.Tick(0.05f, regiments, tuning, state);
    CHECK_FALSE(regiments[0].disengaging);
    CHECK(regiments[0].state == ERegimentState::Marching);
    CHECK(regiments[1].state == ERegimentState::Reforming);
    CHECK(regiments[1].engagedWith.empty());
    CHECK_THAT(regiments[1].anchor.x, Catch::Matchers::WithinAbs(1.5f, 0.001f));
}

TEST_CASE("NextTotalwar synthetic battle is deterministic and resolves",
          "[NextTotalwar][Combat]")
{
    const auto first = RunBattle(1337);
    const auto replay = RunBattle(1337);
    const auto alternate = RunBattle(7331);
    REQUIRE(first.size() == 2000);
    CHECK(first == replay);
    CHECK(first != alternate);
    INFO("final strengths: " << first.back()[0] << ", " << first.back()[1]);
    CHECK((first.back()[0] == 0 || first.back()[1] == 0));
    for (size_t tick = 1; tick < first.size(); ++tick)
    {
        CHECK(first[tick][0] <= first[tick - 1][0]);
        CHECK(first[tick][1] <= first[tick - 1][1]);
    }
}
