#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Modules/ScadLoader/ScadModule.hpp"

// Walkability closure over a gk_terrain scene: the carved river must block the
// nav grid while the bridge deck connects the two banks. Uses the composed
// overhill_valley sample (assets/scad/specs/overhill_valley.json):
//   * bridge deck centered around engine-world (2, ~, 30.5)
//   * river runs roughly north-south through x ~= 0..5 (engine z = -scad y)
TEST_CASE_METHOD(EngineTestFixture, "Terrain walkability: river blocks, bridge connects", "[Integration][Terrain]")
{
    Modules::Scad::Register();
    GOption->KeepCPUMeshData = true; // NavGrid raycasts the CPU BVH

    engine_->RequestLoadScene({.filename = "assets/scad/proc/generated/overhill_valley.scad"});

    // The fixture engine is already Running with its default scene, so poll
    // for the async load result (the terrain component) instead of the status.
    std::shared_ptr<Runtime::TerrainComponent> terrain;
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
    CHECK(terrain->GetCellsX() == 120);
    CHECK(terrain->GetCellsY() == 100);

    // Bank anchors on both sides of the river near the bridge (engine space).
    const glm::vec3 westBank(-12.0f, 0.0f, 30.5f);
    const glm::vec3 eastBank(12.0f, 0.0f, 29.0f);
    const float westH = terrain->SampleHeight(westBank.x, westBank.z);
    const float eastH = terrain->SampleHeight(eastBank.x, eastBank.z);
    CHECK_FALSE(terrain->IsWater(westBank.x, westBank.z));
    CHECK_FALSE(terrain->IsWater(eastBank.x, eastBank.z));

    // Mid-channel south of the bridge is water and unwalkable semantically.
    CHECK(terrain->IsWater(3.0f, 45.0f));
    CHECK_FALSE(terrain->IsWalkable(3.0f, 45.0f));

    // ---- NavGrid over the bridge neighbourhood ----
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = 1.0f;
    settings.maxSlopeAngle = 50.0f;
    settings.maxStepHeight = 0.45f;
    settings.clearanceHeight = 1.8f;
    settings.worldMin = glm::vec3(-25.0f, -8.0f, 15.0f);
    settings.worldMax = glm::vec3(20.0f, 25.0f, 50.0f);

    NextGameplay::FNavGrid navGrid;
    navGrid.Build(engine_->GetScene().GetCPUAccelerationStructure(), settings);
    REQUIRE(navGrid.IsBuilt());

    // Semantic water veto: rays hit the dry bed below the raycast-invisible
    // water surface, and gentle banks are within step height — the terrain
    // component supplies the "this is under water" knowledge. Cells above the
    // water line (the bridge deck) stay walkable.
    navGrid.MaskUnwalkable([&](const glm::vec3& cellPos)
    {
        return terrain->IsWater(cellPos.x, cellPos.z) &&
               cellPos.y < terrain->WaterSurface(cellPos.x, cellPos.z) + 0.05f;
    });

    const glm::vec3 from(westBank.x, westH, westBank.z);
    const glm::vec3 to(eastBank.x, eastH, eastBank.z);
    const std::vector<glm::vec3> path = navGrid.FindPath(from, to, from.y);
    REQUIRE_FALSE(path.empty());

    // The path must run over the bridge deck: some waypoint close to the
    // bridge center, none wading through the river south of it.
    const glm::vec2 bridgeCenter(2.0f, 30.5f);
    bool nearBridge = false;
    for (const glm::vec3& p : path)
    {
        nearBridge = nearBridge || glm::distance(glm::vec2(p.x, p.z), bridgeCenter) < 6.0f;
    }
    CHECK(nearBridge);

    // Every crossing waypoint inside the channel band must be above the water
    // surface (i.e. on the deck), not down on the river bed.
    for (const glm::vec3& p : path)
    {
        if (p.x > -4.0f && p.x < 7.0f && terrain->IsWater(p.x, p.z))
        {
            CHECK(p.y > terrain->WaterSurface(p.x, p.z) - 0.1f);
        }
    }

    // Reachability: mid-river cells away from the bridge stay unreachable from
    // the west bank (banks too steep to step down into the channel).
    const std::vector<uint8_t> reachable = navGrid.BuildReachabilityMask(from, from.y);
    REQUIRE_FALSE(reachable.empty());
    auto cellIndexOf = [&](float worldX, float worldZ)
    {
        const int gx = static_cast<int>(std::floor((worldX - settings.worldMin.x) / settings.cellSize));
        const int gz = static_cast<int>(std::floor((worldZ - settings.worldMin.z) / settings.cellSize));
        return gz * navGrid.GetWidth() + gx;
    };
    int unreachableRiverCells = 0;
    int riverCells = 0;
    for (float z = 40.0f; z <= 48.0f; z += 1.0f)
    {
        for (float x = 0.0f; x <= 6.0f; x += 1.0f)
        {
            if (!terrain->IsWater(x, z)) continue;
            ++riverCells;
            const int idx = cellIndexOf(x, z);
            if (idx >= 0 && idx < static_cast<int>(reachable.size()) && !reachable[idx])
            {
                ++unreachableRiverCells;
            }
        }
    }
    REQUIRE(riverCells > 5);
    CHECK(unreachableRiverCells == riverCells);

    // ---- Physics: terrain mesh collides, water surface does not ----
    // (scene build creates static mesh bodies only for raycast-visible nodes)
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    // A ball dropped over an empty spot on the village pad rests on the
    // terrain surface (engine (58, 23) = pad center area, no buildings).
    const float padH = terrain->SampleHeight(58.0f, 23.0f);
    auto padBall = physics->CreateSphereBody(glm::vec3(58.0f, padH + 5.0f, 23.0f), 0.5f, NextMotionType::Dynamic);
    // A ball dropped over the river sinks through the (collider-less) water
    // surface and rests on the river bed.
    const float riverWater = terrain->WaterSurface(2.0f, 40.0f);
    auto riverBall = physics->CreateSphereBody(glm::vec3(2.0f, riverWater + 5.0f, 40.0f), 0.4f, NextMotionType::Dynamic);

    Simulate(180);

    const auto* padBallInfo = physics->GetBody(padBall);
    REQUIRE(padBallInfo != nullptr);
    CHECK(padBallInfo->position.y == Catch::Approx(padH + 0.5f).margin(0.4f));

    const auto* riverBallInfo = physics->GetBody(riverBall);
    REQUIRE(riverBallInfo != nullptr);
    CHECK(riverBallInfo->position.y < riverWater - 0.2f); // below the water line
    CHECK(riverBallInfo->position.y > riverWater - 4.0f); // resting on the bed, not falling forever

    physics->RemoveBody(padBall);
    physics->RemoveBody(riverBall);
}
