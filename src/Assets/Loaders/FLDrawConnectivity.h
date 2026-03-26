#pragma once
#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"

namespace Assets::LDrawConn
{
    // --- Element type categories (ID1) ---

    enum class EElementCategory : uint16_t
    {
        Point  = 0,  // Pin, axle, clip, bar, hole connectors (8B payload)
        Marker = 1,  // Compatibility tag, no payload
        Grid   = 2,  // Anti-stud / pinhole inner grids (also ID1=3 for stud grids)
        Custom = 4,  // Part-specific connectors (2B payload)
        Tube   = 6,  // Bottom tube connector (17B payload)
    };

    // --- Point connector sub-types (ID2 when ID1=0) ---

    enum class EPointType : uint16_t
    {
        TechnicPinhole    = 2,
        TechnicPin        = 3,
        Unknown5          = 5,
        TechnicAxlehole   = 6,
        TechnicAxle       = 7,
        TechnicAxleStop   = 9,
        RoundHole         = 10,
        BarForRoundHole   = 11,
        Clip              = 12,
        Bar               = 13,
        Unknown14         = 14,
        Unknown17         = 17,
    };

    // --- Grid sub-types ---

    enum class EGridType : uint16_t
    {
        // ID1=2 (anti-stud / receiver surfaces)
        BotAntiStudBrick  = 0,   // (2,0) - bottom at brick height
        BotAntiStudPlate  = 1,   // (2,1) - bottom at plate height
        PinholeInner      = 2,   // (2,2) - inner wall of technic hole

        // ID1=3 (stud / male surfaces)
        FrictionRidge     = 0,   // (3,0) - friction ridges
        TopStud           = 23,  // (3,23) - top stud face
    };

    // --- Grid vertex values ---

    struct FGridVertex
    {
        uint16_t type;
        uint16_t value;

        bool IsStudCenter() const { return (type == 10 || type == 20) && value == 4; }
        bool IsBlocked() const { return type == 0xFFFF; }
    };

    // --- Parsed element data ---

    struct FConnTransform
    {
        glm::mat3 rotation{1.0f};
        glm::vec3 position{0.0f};
    };

    struct FPointConnector
    {
        uint16_t subType = 0;       // EPointType
        FConnTransform transform;
        uint16_t flags = 0;
        float length = 0.0f;
        uint16_t endFlags = 0;
    };

    struct FGridConnector
    {
        uint16_t id1 = 0;           // 2=anti-stud, 3=stud
        uint16_t id2 = 0;           // sub-type
        FConnTransform transform;
        uint16_t gridZ = 0;
        uint16_t gridX = 0;
        std::vector<FGridVertex> vertices;  // (gridZ+1)*(gridX+1)
    };

    struct FConnFile
    {
        std::vector<FPointConnector> points;
        std::vector<FGridConnector> grids;
    };

    // --- Parser API ---

    bool ParseConnFile(const uint8_t* data, size_t size, FConnFile& outFile);
    bool LoadConnFile(const std::string& pakEntry, FConnFile& outFile);
}
