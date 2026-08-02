#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_test_macros.hpp>

#include "Data/DeterministicRng.hpp"
#include "Inventory/Inventory.hpp"
#include "Player/PlayerActionController.hpp"
#include "Player/SurvivalSystem.hpp"
#include "World/LootDirector.hpp"
#include "Combat/CombatSystem.hpp"
#include "Combat/NoiseSystem.hpp"
#include "Zombies/ZombieSystem.hpp"
#include "World/ZombieSpawnDirector.hpp"
#include "Zombies/ZombieVisualPool.hpp"

TEST_CASE("NextDayz deterministic RNG derives stable streams", "[NextDayz][Productization][Rng]")
{
    NextDayz::FDeterministicRng first(91234);
    NextDayz::FDeterministicRng second(91234);
    NextDayz::FDeterministicRng other(91235);

    for (int index = 0; index < 32; ++index)
    {
        REQUIRE(first.NextU32() == second.NextU32());
    }
    REQUIRE(first.NextU32() != other.NextU32());

    auto lootA = NextDayz::FDeterministicRng(77).Derive(0x4C4F4F54ULL);
    auto lootB = NextDayz::FDeterministicRng(77).Derive(0x4C4F4F54ULL);
    auto zombie = NextDayz::FDeterministicRng(77).Derive(0x5A4F4D42ULL);
    REQUIRE(lootA.NextU32() == lootB.NextU32());
    REQUIRE(lootA.NextU32() != zombie.NextU32());
}

TEST_CASE("NextDayz inventory capacity and batches are atomic", "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    REQUIRE(inventory.TotalCapacity() == 10);
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1));
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("radio", "Radio", EItemKind::Misc, 1));
    REQUIRE(inventory.UsedCapacity() == 9);
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1));
    REQUIRE(inventory.UsedCapacity() == 10);
    REQUIRE_FALSE(inventory.TryAdd("water_bottle", "Water Bottle", EItemKind::Consumable, 1));
    REQUIRE(inventory.CountOf("water_bottle") == 0);

    const std::array<FInventoryAddRequest, 2> rejected = {{
        {"bandage", "Bandage", EItemKind::Consumable, 1},
        {"food_can", "Canned Food", EItemKind::Consumable, 1},
    }};
    const int bandagesBefore = inventory.CountOf("bandage");
    REQUIRE_FALSE(inventory.TryAddBatch(rejected));
    REQUIRE(inventory.CountOf("bandage") == bandagesBefore);
    REQUIRE(inventory.CountOf("food_can") == 0);
}

TEST_CASE("NextDayz stack split conserves quantity", "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId ammoId = 0;
    REQUIRE(inventory.TryAdd("ammo_545", "5.45x39", EItemKind::Ammo, 30, &ammoId));
    REQUIRE(inventory.CountOf("ammo_545") == 30);
    FItemInstanceId splitId = 0;
    REQUIRE(inventory.TrySplit(ammoId, 10, splitId));
    REQUIRE(splitId != ammoId);
    REQUIRE(inventory.CountOf("ammo_545") == 30);
    REQUIRE(inventory.FindInstance(ammoId)->count == 20);
    REQUIRE(inventory.FindInstance(splitId)->count == 10);
}

TEST_CASE("NextDayz equipped container cannot be removed without transfer space",
          "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId backpackId = 0;
    REQUIRE(inventory.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &backpackId));
    REQUIRE(inventory.TryEquip(backpackId));
    REQUIRE(inventory.TotalCapacity() == 38);
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("radio", "Radio", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1));
    REQUIRE_FALSE(inventory.TryUnequip(backpackId));
    REQUIRE(inventory.IsClothingEquipped("backpack"));
    REQUIRE(inventory.TotalCapacity() == 38);
    REQUIRE(inventory.CountOf("fuel") == 1);
    REQUIRE(inventory.CountOf("medkit") == 1);
}

TEST_CASE("NextDayz weapon instance retains magazine state through moves",
          "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId pistolId = 0;
    REQUIRE(inventory.TryAdd("pistol", "Makarov", EItemKind::Weapon, 1, &pistolId));
    REQUIRE(inventory.SetLoadedAmmo(pistolId, 7));
    const FContainerId destination = inventory.Containers().back().containerId;
    REQUIRE(inventory.TryMove(pistolId, destination));
    REQUIRE(inventory.LoadedAmmo(pistolId) == 7);
}

TEST_CASE("NextDayz survival metabolism uses simulation seconds and sprint multipliers",
          "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    FSurvivalConfig tuning;
    tuning.StartingHunger = 100.0f;
    tuning.StartingHydration = 100.0f;
    SurvivalSystem idle;
    SurvivalSystem sprint;
    idle.Configure(tuning);
    sprint.Configure(tuning);
    idle.Reset();
    sprint.Reset();
    idle.Update(60.0f, false);
    sprint.Update(60.0f, true);
    REQUIRE(sprint.Snapshot().hunger < idle.Snapshot().hunger);
    REQUIRE(sprint.Snapshot().hydration < idle.Snapshot().hydration);
    REQUIRE(idle.Snapshot().health == 100.0f);
}

TEST_CASE("NextDayz item use commits once and clamps needs", "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId bottleId = 0;
    REQUIRE(inventory.TryAdd("water_bottle", "Water Bottle", EItemKind::Consumable, 1, &bottleId));
    SurvivalSystem survival;
    survival.Configure(FSurvivalConfig{});
    survival.Reset();
    survival.SetNeeds(90.0f, 90.0f, 75.0f);
    REQUIRE(survival.TryUseItem(inventory, bottleId));
    REQUIRE(survival.Snapshot().hydration == 100.0f);
    REQUIRE(inventory.CountOf("water_bottle") == 0);
    REQUIRE_FALSE(survival.TryUseItem(inventory, bottleId));
}

TEST_CASE("NextDayz use actions cancel before commit and emit exactly one commit",
          "[NextDayz][Productization][Actions]")
{
    using namespace NextDayz;
    PlayerActionController actions;
    actions.Configure(FActionConfig{});
    REQUIRE(actions.BeginUse(EPlayerAction::Drink, 42));
    actions.Update(0.4f);
    actions.RequestCancel();
    actions.Update(0.1f);
    REQUIRE_FALSE(actions.ConsumeItemCommitRequest().has_value());

    REQUIRE(actions.BeginUse(EPlayerAction::Drink, 42));
    actions.Update(1.0f);
    const auto commit = actions.ConsumeItemCommitRequest();
    REQUIRE(commit.has_value());
    REQUIRE(commit->instanceId == 42);
    REQUIRE(commit->action == EPlayerAction::Drink);
    actions.Update(1.0f);
    REQUIRE_FALSE(actions.ConsumeItemCommitRequest().has_value());
}

TEST_CASE("NextDayz damage protection and deprivation death are deterministic",
          "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    SurvivalSystem survival;
    survival.Configure(FSurvivalConfig{});
    survival.Reset();
    REQUIRE(survival.ApplyDamage(10.0f, "infected"));
    REQUIRE_FALSE(survival.ApplyDamage(10.0f, "infected"));
    REQUIRE(survival.Snapshot().health == 90.0f);
    survival.Update(0.36f, false);
    REQUIRE(survival.ApplyDamage(5.0f, "infected"));
    survival.SetNeeds(5.0f, 100.0f, 0.0f);
    survival.Update(3.0f, false);
    REQUIRE(survival.Snapshot().lifeState == EPlayerLifeState::Dead);
    REQUIRE(survival.Snapshot().lastDamageSource == "dehydration");
}

TEST_CASE("NextDayz loot director enforces cooldown visibility distance and generation",
          "[NextDayz][Productization][Loot]")
{
    using namespace NextDayz;
    FLootRespawnTuning tuning;
    tuning.foodWaterSeconds = 10.0;
    tuning.minimumOffscreenSeconds = 5.0;
    LootDirector director;
    director.Configure(tuning);
    director.Reset(4, 991);
    const FLootSlotHandle handle = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    const uint32_t firstRoll = director.Resolve(handle)->roll;
    REQUIRE(director.Reserve(handle));
    REQUIRE(director.Commit(handle, 0.0));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 20.0f, false, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, true, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, false, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 4, 4));
    REQUIRE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 3, 4));
    REQUIRE(director.Resolve(handle)->state == ELootSlotState::Available);
    REQUIRE(director.Resolve(handle)->roll != firstRoll);

    director.Reset(5, 991);
    REQUIRE(director.Resolve(handle) == nullptr);
    const FLootSlotHandle replay = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    REQUIRE(director.Resolve(replay)->roll == firstRoll);
}

TEST_CASE("NextDayz zombies hear investigate chase attack and respect stale handles",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(2);
    const FZombieHandle heard = zombies.Spawn(EZombieProfile::Civilian, glm::vec3(10.0f, 0.0f, 0.0f));
    NoiseSystem noises;
    noises.Emit({}, 25.0f, 1.0f, ENoiseType::Rifle);
    zombies.Update(0.1f, {}, {0.0f, 0.0f, 1.0f}, false, {}, noises.Events());
    REQUIRE(zombies.Resolve(heard)->state == EZombieState::Investigate);

    REQUIRE(zombies.Recycle(heard));
    REQUIRE(zombies.Resolve(heard) == nullptr);
    const FZombieHandle attacker = zombies.Spawn(EZombieProfile::Civilian, {});
    REQUIRE(attacker.generation != heard.generation);
    zombies.Resolve(attacker)->forward = {0.0f, 0.0f, 1.0f};
    const glm::vec3 player(0.0f, 0.0f, 1.0f);
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {});
    zombies.Update(0.45f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {});
    const auto damage = zombies.ConsumePlayerDamageRequests();
    REQUIRE(damage.size() == 1);
    REQUIRE(damage.front().source == attacker);
}

TEST_CASE("NextDayz combat resolves hit zones and invalidates recycled proxies",
          "[NextDayz][Productization][Combat]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    CombatSystem combat(zombies);
    const FZombieHandle zombie = zombies.Spawn(EZombieProfile::Civilian, glm::vec3(0.0f, 0.0f, 10.0f));
    combat.HitProxies().Register(77, zombie, EHitZone::Head);
    FWeaponHitEvent hit;
    hit.sequence = 1;
    hit.hitInstanceId = 77;
    hit.origin = {};
    hit.hitPoint = {0.0f, 0.0f, 10.0f};
    hit.baseDamage = 50.0f;
    REQUIRE(combat.ProcessHit(hit, 100.0f));
    REQUIRE(zombies.Resolve(zombie)->state == EZombieState::Dead);
    REQUIRE(zombies.KillCount() == 1);
    REQUIRE(zombies.Recycle(zombie));
    const FZombieHandle reused = zombies.Spawn(EZombieProfile::Civilian, {});
    REQUIRE(reused.generation != zombie.generation);
    REQUIRE_FALSE(combat.ProcessHit(hit, 100.0f));
    REQUIRE(zombies.Resolve(reused)->health == ZombieDef(EZombieProfile::Civilian).maxHealth);
}

TEST_CASE("NextDayz zombie navigation caches paths and replans after target movement",
          "[NextDayz][Productization][Zombie][Navigation]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    const FZombieHandle handle = zombies.Spawn(EZombieProfile::Civilian, {});
    zombies.Resolve(handle)->forward = {0.0f, 0.0f, 1.0f};
    int pathRequests = 0;
    const ZombieSystem::FPathResolver resolvePath = [&pathRequests](const glm::vec3& from, const glm::vec3& to)
    {
        ++pathRequests;
        return std::vector<glm::vec3>{from, to};
    };
    glm::vec3 player(0.0f, 0.0f, 10.0f);
    for (int tick = 0; tick < 5; ++tick)
    {
        zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                       [](const glm::vec3&, const glm::vec3&) { return true; }, {}, resolvePath);
    }
    CHECK(pathRequests == 1);
    CHECK(zombies.Resolve(handle)->position.z > 0.0f);

    player.x += 3.0f;
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {}, resolvePath);
    CHECK(pathRequests == 2);
}

TEST_CASE("NextDayz spawn director filters safe distance visibility navigation and cap",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(4);
    ZombieSpawnDirector director;
    director.Reset(42);
    director.SetPoints({
        {{20.0f, 0.0f, 0.0f}, EZombieProfile::Civilian},
        {{50.0f, 0.0f, 0.0f}, EZombieProfile::Military},
        {{70.0f, 0.0f, 0.0f}, EZombieProfile::Industrial},
    });
    director.Update(2.0f, {}, zombies,
        [](const glm::vec3& position) { return position.x == 50.0f; },
        [](const glm::vec3& position) { return position.x != 20.0f; });
    REQUIRE(zombies.ActiveCount() == 1);
    REQUIRE(director.SpawnCount() == 1);
    REQUIRE(director.RejectedVisible() == 1);
}

TEST_CASE("NextDayz zombie visual pool is fixed capacity and reusable",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieVisualPool pool(1);
    const FZombieHandle first{0, 1};
    const FZombieHandle second{1, 1};
    REQUIRE(pool.Acquire(first) != nullptr);
    REQUIRE(pool.Acquire(second) == nullptr);
    REQUIRE(pool.ActiveCount() == 1);
    REQUIRE(pool.Release(first));
    REQUIRE(pool.Acquire(second) != nullptr);
    REQUIRE(pool.Capacity() == 1);
}
