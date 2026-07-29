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
    constexpr int count = 64;
    constexpr int ranks = 4;
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
    CHECK_THAT(extent.x, Catch::Matchers::WithinAbs(8.625f, 0.001f));
    CHECK_THAT(extent.y, Catch::Matchers::WithinAbs(2.025f, 0.001f));
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
