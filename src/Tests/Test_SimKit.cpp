#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.h"
#include "Gameplay/Sim/AnchorMap.h"
#include "Gameplay/Sim/CharacterPool.h"
#include "Gameplay/Sim/ScadRigVisual.h"
#include "Gameplay/Sim/SimVisual.h"

#include <glm/gtc/quaternion.hpp>

using namespace NextGameplay::Sim;

TEST_CASE("SimKit anchor map parses and manages POIs", "[Unit][SimKit]")
{
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    nodes.push_back(Assets::Node::CreateNode(
        "desk_engineer_01", glm::vec3(1.0f, 0.2f, 3.0f),
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.0f), 7));
    nodes.push_back(Assets::Node::CreateNode(
        "wait_01", glm::vec3(4.0f, 0.2f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f), 8));
    nodes.push_back(Assets::Node::CreateNode(
        "wall", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), 9));

    FAnchorParseConfig config;
    config.acceptCategories = {"desk", "wait"};
    FAnchorMap map;
    map.BuildFromNodes(nodes, config);

    REQUIRE(map.Count() == 2);
    REQUIRE(map.PointsOfCategory("desk").size() == 1);
    const FAnchorPoi* desk = map.FindByName("desk_engineer_01");
    REQUIRE(desk != nullptr);
    CHECK(desk->nodeId == 7);
    CHECK(desk->frontDir.x == Catch::Approx(1.0f).margin(1e-5f));

    REQUIRE(map.ClaimFree("desk", 42) != nullptr);
    CHECK(map.ClaimFree("desk", 43) == nullptr);
    map.Release("desk_engineer_01", 42);
    REQUIRE(map.ClaimFree("desk", 43) != nullptr);

    glm::vec3 seatPosition{0.0f};
    CHECK(map.ClaimSeat("wait_01", 5, seatPosition) == 0);
    map.ReleaseSeat("wait_01", 0, 5);
    CHECK(map.FindByName("wait_01")->seatOccupied[0] == -1);

    map.SetEnabled("desk", false);
    map.Release("desk_engineer_01", 43);
    CHECK(map.ClaimFree("desk", 44) == nullptr);
    map.ResetEnabled();
    CHECK(map.ClaimFree("desk", 44) != nullptr);
}

TEST_CASE("SimKit character pool acquires moves and releases", "[Unit][SimKit]")
{
    FCharacterPoolConfig config;
    config.poolCapacity = 2;
    config.groundY = 0.0f;
    config.separationRadius = 1.0f;
    config.separationStrength = 2.0f;

    FCharacterPool pool;
    pool.Configure(config);
    FSimCharacter* first = pool.Acquire(0, glm::vec3(0.0f), glm::vec3(1.0f));
    FSimCharacter* second = pool.Acquire(1, glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(1.0f));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(pool.Acquire(0, glm::vec3(0.0f), glm::vec3(1.0f)) == nullptr);

    CHECK_FALSE(pool.MoveTo(*first, glm::vec3(3.0f, 0.0f, 0.0f)));
    CHECK_FALSE(pool.MoveTo(*second, glm::vec3(3.0f, 0.0f, 0.0f)));
    const float initialDistance = glm::distance(first->position, second->position);
    const glm::vec3 firstStart = first->position;
    const glm::vec3 secondStart = second->position;
    pool.Tick(0.2f);
    CHECK((glm::distance(first->position, firstStart) > 0.0f ||
           glm::distance(second->position, secondStart) > 0.0f));
    CHECK(glm::distance(first->position, second->position) > initialDistance);

    pool.Release(*first);
    CHECK_FALSE(first->active);
    CHECK(pool.Acquire(0, glm::vec3(1.0f), glm::vec3(0.5f)) == first);
}

TEST_CASE("SimKit visual hints map to geometry and rig clips", "[Unit][SimKit]")
{
    auto node = Assets::Node::CreateNode(
        "visual", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), 1);
    FGeometryVisual visual(node, glm::vec3(0.0f, -100.0f, 0.0f));
    visual.SetAnimHint(EAnimHint::Sit);
    CHECK(visual.CurrentHint() == EAnimHint::Sit);
    CHECK(node->Scale().y == Catch::Approx(0.55f));
    visual.SetAnimHint(EAnimHint::Work);
    CHECK(node->Scale().y == Catch::Approx(1.0f));

    CHECK(std::string(FScadRigVisual::ClipName(EAnimHint::Idle)) == "idle");
    CHECK(std::string(FScadRigVisual::ClipName(EAnimHint::Walk)) == "walk");
    CHECK(std::string(FScadRigVisual::ClipName(EAnimHint::Sit)) == "sit");
    CHECK(std::string(FScadRigVisual::ClipName(EAnimHint::Work)) == "work");
}
