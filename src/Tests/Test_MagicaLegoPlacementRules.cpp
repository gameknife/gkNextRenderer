#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include "Application/Game/MagicaLego/MagicaLegoPlacementRules.hpp"
#include "Application/Game/MagicaLego/MagicaLegoGameInstance.hpp"
#include <set>
#include <tuple>

namespace
{
    std::set<std::tuple<int16_t, int16_t, int16_t>> ToCellSet(const std::vector<glm::i16vec3>& cells)
    {
        std::set<std::tuple<int16_t, int16_t, int16_t>> out;
        for (const glm::i16vec3& cell : cells)
        {
            out.emplace(cell.x, cell.y, cell.z);
        }
        return out;
    }
}

TEST_CASE("MagicaLego Placement Rules - Thin Type Detection", "[Placement]")
{
    REQUIRE(MagicaLego::Placement::IsThinBlockType("Flat1x1"));
    REQUIRE(MagicaLego::Placement::IsThinBlockType("Plate1x1"));
    REQUIRE(MagicaLego::Placement::IsThinBlockType("Plate2x2"));
    REQUIRE_FALSE(MagicaLego::Placement::IsThinBlockType("Corner2x2"));
    REQUIRE_FALSE(MagicaLego::Placement::IsThinBlockType("Block1x1"));
}

TEST_CASE("MagicaLego Placement Rules - Plate2x2 Footprint", "[Placement]")
{
    glm::i16vec3 anchor{10, 2, 8};
    auto northCells = MagicaLego::Placement::BuildOccupiedCells("Plate2x2", anchor, EOrientation::EO_North);
    auto northSet = ToCellSet(northCells);

    REQUIRE(northSet.size() == 4);
    REQUIRE(northSet.count({9, 2, 8}) == 1);
    REQUIRE(northSet.count({10, 2, 8}) == 1);
    REQUIRE(northSet.count({9, 2, 9}) == 1);
    REQUIRE(northSet.count({10, 2, 9}) == 1);
}

TEST_CASE("MagicaLego Placement Rules - Corner2x2 Occupancy Count", "[Placement]")
{
    glm::i16vec3 anchor{0, 0, 0};

    auto northCells = MagicaLego::Placement::BuildOccupiedCells("Corner2x2", anchor, EOrientation::EO_North);
    auto eastCells = MagicaLego::Placement::BuildOccupiedCells("Corner2x2", anchor, EOrientation::EO_East);

    REQUIRE(ToCellSet(northCells).size() == 3);
    REQUIRE(ToCellSet(eastCells).size() == 3);
}

TEST_CASE("MagicaLego Placement Rules - Slope1x2 Simplified Footprint", "[Placement]")
{
    glm::i16vec3 anchor{5, 1, 5};

    auto northCells = MagicaLego::Placement::BuildOccupiedCells("Slope1x2", anchor, EOrientation::EO_North);
    auto northSet = ToCellSet(northCells);
    REQUIRE(northSet.size() == 2);
    REQUIRE(northSet.count({5, 1, 5}) == 1);
    REQUIRE(northSet.count({5, 1, 6}) == 1);

    auto eastCells = MagicaLego::Placement::BuildOccupiedCells("Slope1x2", anchor, EOrientation::EO_East);
    auto eastSet = ToCellSet(eastCells);
    REQUIRE(eastSet.size() == 2);
    REQUIRE(eastSet.count({5, 1, 5}) == 1);
    REQUIRE((eastSet.count({4, 1, 5}) == 1 || eastSet.count({6, 1, 5}) == 1));
}
