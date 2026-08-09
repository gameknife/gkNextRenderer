#pragma once

#include "NextTotalwarTypes.h"

#include <cstdint>
#include <vector>

namespace NextTotalwar
{
    struct FCombatGridEntry
    {
        int16_t regiment = -1;
        int16_t soldier = -1;

        friend bool operator==(const FCombatGridEntry&, const FCombatGridEntry&) = default;
    };

    class FCombatGrid
    {
    public:
        explicit FCombatGrid(float cellSize = 2.0f, float worldHalfExtent = 200.0f);

        void Build(const std::vector<FRegiment>& regiments);
        void Query(const glm::vec3& position, float radius,
                   std::vector<FCombatGridEntry>& results) const;

    private:
        struct FStoredEntry
        {
            FCombatGridEntry value;
            glm::vec3 position{};
            int next = -1;
        };

        int CellX(float x) const;
        int CellZ(float z) const;
        int CellIndex(int x, int z) const;

        float cellSize_;
        float worldHalfExtent_;
        int dimension_;
        std::vector<int> heads_;
        std::vector<FStoredEntry> entries_;
    };
}
