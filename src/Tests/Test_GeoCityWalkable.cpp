#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Modules/ScadLoader/ScadModule.hpp"

// Walkability closure over a city generated from real geographic data
// (`gnb geo make --name hk_victoria --at 22.2855,114.1580 --size 1000
// --profile hongkong`, see docs/designs/geo-city-generation-design.md).
//
// This is the test that decides whether the generator emits a *level* rather
// than a diorama: the terrain has to be navigable, the extruded footprints have
// to behave as solid obstacles for both the nav grid and the physics engine,
// and the harbour has to stay off limits.
//
// Coordinates come from the committed tile and are stable because the whole
// pipeline is deterministic (regenerating produces a byte-identical .scad).
// Engine space is (x, z) = (scad x, -scad y); world Y is scad z.
namespace
{
    // Connaught Road Central, an open arterial on the reclaimed flat.
    constexpr glm::vec2 kStreetA(-156.1f, 43.4f);
    // Queen Victoria Street, ~108 m away and 17 m higher up the slope.
    constexpr glm::vec2 kStreetB(-194.5f, 144.6f);
    // Deepest interior point of the Hang Seng Bank HQ footprint (137 m, tagged),
    // which sits between the two street points.
    constexpr glm::vec2 kTowerInside(-183.7f, 95.6f);
    // Mid-harbour, well north of the waterfront.
    constexpr glm::vec2 kHarbour(0.0f, -350.0f);

    // Above every roof in the test region (tallest is ~170 m).
    constexpr float kSampleCeiling = 260.0f;
}

TEST_CASE_METHOD(EngineTestFixture, "Geo city walkability: streets connect, towers block, harbour is off limits",
                 "[GPU][Integration][Terrain][Geo]")
{
    Modules::Scad::Register();
    GOption->KeepCPUMeshData = true; // NavGrid raycasts the CPU BVH

    engine_->RequestLoadScene({.filename = "assets/scad/proc/generated/hk_victoria.scad"});

    Runtime::TerrainComponent* terrain = nullptr;
    for (int i = 0; i < 2400 && !terrain; ++i)
    {
        Simulate(1);
        for (const std::shared_ptr<Assets::Node>& node : engine_->GetScene().Nodes())
        {
            if (auto comp = node->GetComponent<Runtime::TerrainComponent>())
            {
                terrain = comp;
            }
        }
    }
    REQUIRE(terrain != nullptr);
    REQUIRE(engine_->GetEngineStatus() == NextRenderer::EApplicationStatus::Running);
    Simulate(5);
    REQUIRE(terrain->HasData());
    CHECK(terrain->GetCellsX() == 176);
    CHECK(terrain->GetCellsY() == 176);

    // ---- Terrain semantics: sea vs. land ----
    CHECK(terrain->IsWater(kHarbour.x, kHarbour.y));
    CHECK_FALSE(terrain->IsWalkable(kHarbour.x, kHarbour.y));
    // The dredged bed is well below the water plane, so boats float and dropped
    // objects sink instead of resting on a shoal.
    CHECK(terrain->SampleHeight(kHarbour.x, kHarbour.y) < -5.0f);
    CHECK(terrain->WaterSurface(kHarbour.x, kHarbour.y) == Catch::Approx(0.0f).margin(0.2f));

    const float streetAHeight = terrain->SampleHeight(kStreetA.x, kStreetA.y);
    const float streetBHeight = terrain->SampleHeight(kStreetB.x, kStreetB.y);
    CHECK_FALSE(terrain->IsWater(kStreetA.x, kStreetA.y));
    CHECK_FALSE(terrain->IsWater(kStreetB.x, kStreetB.y));
    // Dry land never dips below the water plane (see design §5.1).
    CHECK(streetAHeight > 0.0f);
    CHECK(streetBHeight > 0.0f);

    // ---- NavGrid over the downtown block that holds both street points ----
    // Deliberately a sub-region: a whole 1 km tile at 1 m cells is a million
    // raycast columns and nothing about the closure needs them. It does have to
    // be wide enough to contain a street-level route, though: in a downtown this
    // dense, a box drawn tightly around the two endpoints is walled off by the
    // blocks between them and the path search correctly finds nothing.
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = 1.0f;
    settings.maxSlopeAngle = 50.0f;
    settings.maxStepHeight = 0.6f;
    settings.clearanceHeight = 2.0f;
    settings.sampleCeiling = kSampleCeiling;
    // floorHeightTolerance is an absolute band around the query's reference
    // height, not a locally propagated one: it exists to keep the storeys of a
    // building apart. On sloping ground it has to span the region's relief, or
    // every cell more than a metre uphill counts as a different floor and
    // nothing is reachable. Here the street rises ~15 m between the two
    // endpoints; 60 m still leaves the 137 m tower roof out of reach.
    settings.floorHeightTolerance = 60.0f;
    settings.worldMin = glm::vec3(-270.0f, 0.0f, -20.0f);
    settings.worldMax = glm::vec3(-70.0f, 200.0f, 180.0f);

    NextGameplay::FNavGrid navGrid;
    navGrid.Build(engine_->GetScene().GetCPUAccelerationStructure(), settings);
    REQUIRE(navGrid.IsBuilt());

    navGrid.MaskUnwalkable([&](const glm::vec3& cellPos)
    {
        return terrain->IsWater(cellPos.x, cellPos.z) &&
               cellPos.y < terrain->WaterSurface(cellPos.x, cellPos.z) + 0.05f;
    });

    auto cellOf = [&](const glm::vec2& world)
    {
        return glm::ivec2(
            static_cast<int>(std::floor((world.x - settings.worldMin.x) / settings.cellSize)),
            static_cast<int>(std::floor((world.y - settings.worldMin.z) / settings.cellSize)));
    };

    // The extruded footprint is solid: the down-ray lands on the roof, more than
    // a storey above the street, so the cell is not on the street's floor.
    const glm::ivec2 towerCell = cellOf(kTowerInside);
    CHECK_FALSE(navGrid.IsCellReachable(towerCell.x, towerCell.y, streetAHeight));

    // ---- Streets connect ----
    const glm::vec3 from(kStreetA.x, streetAHeight, kStreetA.y);
    const glm::vec3 to(kStreetB.x, streetBHeight, kStreetB.y);

    const std::vector<glm::vec3> path = navGrid.FindPath(from, to, from.y);
    REQUIRE_FALSE(path.empty());

    // The route goes around the tower, never through it. The footprint bounding
    // box is (engine) x -205.3..-158.1, z 73.9..123.1; allow a small margin so
    // a waypoint hugging the facade does not trip the check.
    for (const glm::vec3& p : path)
    {
        const bool insideFootprint = p.x > -203.0f && p.x < -160.0f &&
                                     p.z > 76.0f && p.z < 121.0f;
        CHECK_FALSE(insideFootprint);
    }

    // Reachability from the street cannot leak into the building either.
    const std::vector<uint8_t> reachable = navGrid.BuildReachabilityMask(from, from.y);
    REQUIRE_FALSE(reachable.empty());
    const int towerIdx = towerCell.y * navGrid.GetWidth() + towerCell.x;
    REQUIRE(towerIdx >= 0);
    REQUIRE(towerIdx < static_cast<int>(reachable.size()));
    CHECK(reachable[towerIdx] == 0);

    // ---- Physics: the street carries an actor, the tower is solid ----
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    auto streetBall = physics->CreateSphereBody(
        glm::vec3(kStreetA.x, streetAHeight + 6.0f, kStreetA.y), 0.5f, NextMotionType::Dynamic);
    // Dropped from above every roof: if the footprints were not cooked into
    // collision shapes this would fall all the way to the terrain.
    auto roofBall = physics->CreateSphereBody(
        glm::vec3(kTowerInside.x, 200.0f, kTowerInside.y), 0.5f, NextMotionType::Dynamic);

    Simulate(420);

    const auto* streetBallInfo = physics->GetBody(streetBall);
    REQUIRE(streetBallInfo != nullptr);
    // The street surface is a 12 cm slab laid on the terrain, so the rest height
    // is a little above the raw terrain sample.
    CHECK(streetBallInfo->position.y > streetAHeight);
    CHECK(streetBallInfo->position.y < streetAHeight + 1.5f);

    const auto* roofBallInfo = physics->GetBody(roofBall);
    REQUIRE(roofBallInfo != nullptr);
    const float towerGround = terrain->SampleHeight(kTowerInside.x, kTowerInside.y);
    CHECK(roofBallInfo->position.y > towerGround + 90.0f);  // it stopped on the roof
    CHECK(roofBallInfo->position.y < towerGround + 145.0f); // and not in mid-air

    physics->RemoveBody(streetBall);
    physics->RemoveBody(roofBall);
}
