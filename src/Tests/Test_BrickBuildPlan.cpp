#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/BrickPlayer/BrickBuildPlan.hpp"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

TEST_CASE("BrickBuildPlan validates parts inventory connections and collisions", "[Unit][AI][BrickPlayer]")
{
    const auto json = nlohmann::json::parse(R"json({"version":1,"parts":[{"id":"a","part_id":"3001.dat","color":4,"position":[0,0,0],"rotation_degrees":[0,0,0]},{"id":"b","part_id":"3001.dat","color":4,"position":[0,1,0],"rotation_degrees":[0,0,0]}],"connections":[{"part_a":"a","connector_a":"top","part_b":"b","connector_b":"bottom"}]})json");
    const auto plan = BrickPlayer::ParseBrickBuildPlan(json);
    BrickPlayer::FBrickBuildConstraints constraints;
    constraints.availablePartIds.insert("3001.dat");
    constraints.inventory["3001.dat"] = 2;
    constraints.overlaps = [](const auto&, const auto&) { return false; };
    constraints.connectionExists = [](const auto&) { return true; };
    REQUIRE(BrickPlayer::ValidateBrickBuildPlan(plan, constraints).valid);
    constraints.inventory["3001.dat"] = 1;
    REQUIRE_FALSE(BrickPlayer::ValidateBrickBuildPlan(plan, constraints).valid);
}
