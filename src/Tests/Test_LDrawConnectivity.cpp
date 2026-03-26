#include "Assets/Loaders/FLDrawConnectivity.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstring>
#include <vector>

using namespace Assets::LDrawConn;
using Catch::Matchers::WithinAbs;

namespace
{
    // Helper to build a binary buffer for testing
    class FConnBuilder
    {
    public:
        void WriteUInt16(uint16_t val)
        {
            const auto* ptr = reinterpret_cast<const uint8_t*>(&val);
            data_.insert(data_.end(), ptr, ptr + 2);
        }

        void WriteFloat(float val)
        {
            const auto* ptr = reinterpret_cast<const uint8_t*>(&val);
            data_.insert(data_.end(), ptr, ptr + 4);
        }

        void WriteIdentityMatrix()
        {
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    WriteFloat(row == col ? 1.0f : 0.0f);
                }
            }
        }

        void WritePosition(float x, float y, float z)
        {
            WriteFloat(x);
            WriteFloat(y);
            WriteFloat(z);
        }

        // Write a point element (ID1=0): header + 8-byte payload
        void WritePointElement(uint16_t subType, float px, float py, float pz,
                               uint16_t flags, float length, uint16_t endFlags)
        {
            WriteUInt16(0);          // ID1 = Point
            WriteUInt16(subType);    // ID2
            WriteIdentityMatrix();
            WritePosition(px, py, pz);
            WriteUInt16(flags);
            WriteFloat(length);      // UNALIGNED float at offset +2 within payload
            WriteUInt16(endFlags);
        }

        // Write a grid element (ID1=2 or 3)
        void WriteGridElement(uint16_t id1, uint16_t id2, float px, float py, float pz,
                              uint16_t gridZ, uint16_t gridX,
                              const std::vector<std::pair<uint16_t, uint16_t>>& vertices)
        {
            WriteUInt16(id1);
            WriteUInt16(id2);
            WriteIdentityMatrix();
            WritePosition(px, py, pz);
            WriteUInt16(gridZ);
            WriteUInt16(gridX);
            for (const auto& [type, value] : vertices)
            {
                WriteUInt16(type);
                WriteUInt16(value);
            }
        }

        const uint8_t* Data() const { return data_.data(); }
        size_t Size() const { return data_.size(); }

    private:
        std::vector<uint8_t> data_;
    };
}

TEST_CASE("LDrawConn: parse point element (Technic Axle)", "[Unit][LDrawConn]")
{
    FConnBuilder builder;
    builder.WritePointElement(7, 50.0f, 0.0f, 0.0f, 0, 100.0f, 0);

    FConnFile file;
    REQUIRE(ParseConnFile(builder.Data(), builder.Size(), file));
    REQUIRE(file.points.size() == 1);
    REQUIRE(file.grids.empty());

    const auto& point = file.points[0];
    CHECK(point.subType == 7);
    CHECK_THAT(point.transform.position.x, WithinAbs(50.0f, 0.01f));
    CHECK_THAT(point.transform.position.y, WithinAbs(0.0f, 0.01f));
    CHECK(point.flags == 0);
    CHECK_THAT(point.length, WithinAbs(100.0f, 0.01f));
    CHECK(point.endFlags == 0);
}

TEST_CASE("LDrawConn: parse grid element (1x1 top stud)", "[Unit][LDrawConn]")
{
    // 1x1 brick top stud: grid 2x2, 3x3 vertices
    // Stud centers at corners (3,1), edges (0,4), center (10,4)
    std::vector<std::pair<uint16_t, uint16_t>> verts = {
        {3, 1}, {0, 4}, {3, 1},
        {0, 4}, {10, 4}, {0, 4},
        {3, 1}, {0, 4}, {3, 1},
    };

    FConnBuilder builder;
    builder.WriteGridElement(3, 23, -10.0f, 0.0f, 10.0f, 2, 2, verts);

    FConnFile file;
    REQUIRE(ParseConnFile(builder.Data(), builder.Size(), file));
    REQUIRE(file.grids.size() == 1);
    REQUIRE(file.points.empty());

    const auto& grid = file.grids[0];
    CHECK(grid.id1 == 3);
    CHECK(grid.id2 == 23);
    CHECK(grid.gridZ == 2);
    CHECK(grid.gridX == 2);
    CHECK(grid.vertices.size() == 9);

    // Center vertex should be a stud center
    CHECK(grid.vertices[4].IsStudCenter());

    // Corner vertex should not be a stud center
    CHECK_FALSE(grid.vertices[0].IsStudCenter());
}

TEST_CASE("LDrawConn: parse mixed file (brick with pinhole)", "[Unit][LDrawConn]")
{
    FConnBuilder builder;

    // Section 0: bottom anti-stud grid (2,0)
    std::vector<std::pair<uint16_t, uint16_t>> botVerts = {
        {0, 1}, {0, 2}, {0, 1},
        {0, 2}, {10, 4}, {0, 2},
        {0, 1}, {0, 2}, {0, 1},
    };
    builder.WriteGridElement(2, 0, -10.0f, 24.0f, 10.0f, 2, 2, botVerts);

    // Section 1: top stud grid (3,23)
    std::vector<std::pair<uint16_t, uint16_t>> topVerts = {
        {3, 1}, {0, 4}, {3, 1},
        {0, 4}, {10, 4}, {0, 4},
        {3, 1}, {0, 4}, {3, 1},
    };
    builder.WriteGridElement(3, 23, -10.0f, 0.0f, 10.0f, 2, 2, topVerts);

    // Section 2: pinhole (0,2)
    builder.WritePointElement(2, 0.0f, 10.0f, -10.0f, 0, 20.0f, 0);

    FConnFile file;
    REQUIRE(ParseConnFile(builder.Data(), builder.Size(), file));
    CHECK(file.grids.size() == 2);
    CHECK(file.points.size() == 1);

    // Verify pinhole
    CHECK(file.points[0].subType == 2);
    CHECK_THAT(file.points[0].length, WithinAbs(20.0f, 0.01f));
}

TEST_CASE("LDrawConn: empty/truncated data returns false", "[Unit][LDrawConn]")
{
    FConnFile file;

    CHECK_FALSE(ParseConnFile(nullptr, 0, file));
    CHECK_FALSE(ParseConnFile(nullptr, 100, file));

    // Truncated header (less than 52 bytes)
    uint8_t shortData[40] = {};
    CHECK_FALSE(ParseConnFile(shortData, sizeof(shortData), file));
}

TEST_CASE("LDrawConn: skip marker (ID1=1) and tube (ID1=6) elements", "[Unit][LDrawConn]")
{
    FConnBuilder builder;

    // Marker element (ID1=1, 0-byte payload)
    builder.WriteUInt16(1);   // ID1
    builder.WriteUInt16(5);   // ID2
    builder.WriteIdentityMatrix();
    builder.WritePosition(0.0f, 0.0f, 0.0f);
    // No payload for markers

    // A normal point element after
    builder.WritePointElement(13, 0.0f, 10.0f, 0.0f, 0, 19.0f, 0);

    FConnFile file;
    REQUIRE(ParseConnFile(builder.Data(), builder.Size(), file));
    CHECK(file.points.size() == 1);
    CHECK(file.points[0].subType == 13);  // Bar
}

TEST_CASE("LDrawConn: grid vertex IsStudCenter and IsBlocked", "[Unit][LDrawConn]")
{
    FGridVertex studCenter10{10, 4};
    FGridVertex studCenter20{20, 4};
    FGridVertex blocked{0xFFFF, 0};
    FGridVertex edge{0, 4};
    FGridVertex wall{3, 1};

    CHECK(studCenter10.IsStudCenter());
    CHECK(studCenter20.IsStudCenter());
    CHECK(blocked.IsBlocked());
    CHECK_FALSE(edge.IsStudCenter());
    CHECK_FALSE(wall.IsStudCenter());
    CHECK_FALSE(edge.IsBlocked());
}
