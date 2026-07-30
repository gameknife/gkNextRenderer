#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Battle/FormationLayout.h"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadRig.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <set>
#include <string>
#include <vector>

TEST_CASE("NextTotalwar formation layout is centered and deterministic", "[NextTotalwar][Formation]")
{
    constexpr int count = 100;
    constexpr int ranks = 10;
    std::set<std::pair<int, int>> quantized;
    for (int index = 0; index < count; ++index)
    {
        const glm::vec2 first = NextTotalwar::Formation::SlotLocalOffset(index, count, ranks, 1.15f, 1.35f);
        const glm::vec2 second = NextTotalwar::Formation::SlotLocalOffset(index, count, ranks, 1.15f, 1.35f);
        CHECK(first == second);
        quantized.emplace(static_cast<int>(first.x * 1000.0f), static_cast<int>(first.y * 1000.0f));
    }
    CHECK(quantized.size() == count);

    const glm::vec2 extent = NextTotalwar::Formation::FormationHalfExtent(count, ranks, 1.15f, 1.35f);
    CHECK_THAT(extent.x, Catch::Matchers::WithinAbs(5.175f, 0.001f));
    CHECK_THAT(extent.y, Catch::Matchers::WithinAbs(6.075f, 0.001f));
}

TEST_CASE("NextTotalwar formation world transform rotates around anchor", "[NextTotalwar][Formation]")
{
    const glm::vec3 anchor(12.0f, 3.0f, -8.0f);
    const glm::vec2 local(2.0f, -4.0f);
    const glm::vec3 unrotated = NextTotalwar::Formation::SlotWorld(anchor, 0.0f, local);
    const glm::vec3 rotated = NextTotalwar::Formation::SlotWorld(anchor, glm::half_pi<float>(), local);

    CHECK_THAT(unrotated.x, Catch::Matchers::WithinAbs(14.0f, 0.001f));
    CHECK_THAT(unrotated.z, Catch::Matchers::WithinAbs(-12.0f, 0.001f));
    CHECK_THAT(rotated.x, Catch::Matchers::WithinAbs(8.0f, 0.001f));
    CHECK_THAT(rotated.z, Catch::Matchers::WithinAbs(-10.0f, 0.001f));
    CHECK_THAT(rotated.y, Catch::Matchers::WithinAbs(anchor.y, 0.001f));
    const glm::vec2 restored =
        NextTotalwar::Formation::SlotLocal(anchor, glm::half_pi<float>(), rotated);
    CHECK_THAT(restored.x, Catch::Matchers::WithinAbs(local.x, 0.001f));
    CHECK_THAT(restored.y, Catch::Matchers::WithinAbs(local.y, 0.001f));
}

TEST_CASE("NextTotalwar assigns selected regiments to minimum-travel destinations",
          "[NextTotalwar][Formation]")
{
    const std::vector<glm::vec3> starts = {
        {0.0f, 0.0f, 30.0f},
        {0.0f, 0.0f, 10.0f},
        {0.0f, 0.0f, -10.0f},
        {0.0f, 0.0f, -30.0f},
    };
    const std::vector<glm::vec3> destinations = {
        {100.0f, 0.0f, -30.0f},
        {100.0f, 0.0f, -10.0f},
        {100.0f, 0.0f, 10.0f},
        {100.0f, 0.0f, 30.0f},
    };

    const std::vector<size_t> assignment =
        NextTotalwar::Formation::MinimumTravelAssignment(starts, destinations);
    REQUIRE(assignment.size() == starts.size());
    CHECK(assignment == std::vector<size_t>{3, 2, 1, 0});

    float assignedDistance = 0.0f;
    float indexOrderedDistance = 0.0f;
    for (size_t index = 0; index < starts.size(); ++index)
    {
        assignedDistance += glm::distance(starts[index], destinations[assignment[index]]);
        indexOrderedDistance += glm::distance(starts[index], destinations[index]);
    }
    CHECK(assignedDistance < indexOrderedDistance);
}

TEST_CASE("NextTotalwar shipped rigs satisfy the six-part reusable mesh budget", "[NextTotalwar][ScadRig]")
{
    for (const char* path : {
             "assets/scad/characters/tw_spearman.scad",
             "assets/scad/characters/tw_swordsman.scad",
             "assets/scad/characters/tw_archer.scad"})
    {
        Assets::FRigAsset asset;
        std::string error;
        std::vector<std::string> warnings;
        INFO(path);
        REQUIRE(Assets::FScadRigLoader::LoadRig(path, {}, asset, error, &warnings));
        CHECK(warnings.empty());
        CHECK(asset.bones.size() == 7);
        CHECK(asset.parts.size() == 6);
        CHECK(asset.partModels.size() == 6);
        for (const char* clip : {"idle", "walk", "march", "run"})
        {
            CHECK(asset.FindClip(clip) != nullptr);
        }

        size_t triangles = 0;
        for (const Assets::Model& model : asset.partModels)
        {
            triangles += model.NumberOfIndices() / 3;
        }
        CHECK(triangles <= 300);
    }
}
