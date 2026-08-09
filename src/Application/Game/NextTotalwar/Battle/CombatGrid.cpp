#include "Battle/CombatGrid.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace NextTotalwar
{
    FCombatGrid::FCombatGrid(float cellSize, float worldHalfExtent)
        : cellSize_(std::max(cellSize, 0.1f)),
          worldHalfExtent_(std::max(worldHalfExtent, cellSize_)),
          dimension_(static_cast<int>(std::ceil(worldHalfExtent_ * 2.0f / cellSize_)) + 1),
          heads_(static_cast<size_t>(dimension_ * dimension_), -1)
    {
        entries_.reserve(4096);
    }

    int FCombatGrid::CellX(float x) const
    {
        return glm::clamp(static_cast<int>(std::floor((x + worldHalfExtent_) / cellSize_)),
                          0, dimension_ - 1);
    }

    int FCombatGrid::CellZ(float z) const
    {
        return glm::clamp(static_cast<int>(std::floor((z + worldHalfExtent_) / cellSize_)),
                          0, dimension_ - 1);
    }

    int FCombatGrid::CellIndex(int x, int z) const
    {
        return z * dimension_ + x;
    }

    void FCombatGrid::Build(const std::vector<FRegiment>& regiments)
    {
        std::fill(heads_.begin(), heads_.end(), -1);
        entries_.clear();
        std::vector<bool> included(regiments.size(), false);
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            if (!regiments[regimentIndex].engagedWith.empty())
            {
                included[regimentIndex] = true;
            }
            for (const int16_t target : regiments[regimentIndex].engagedWith)
            {
                if (target >= 0 && static_cast<size_t>(target) < regiments.size())
                {
                    included[static_cast<size_t>(target)] = true;
                }
            }
        }
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            const FRegiment& regiment = regiments[regimentIndex];
            if (!included[regimentIndex] || regiment.state == ERegimentState::Destroyed) continue;
            for (size_t soldierIndex = 0; soldierIndex < regiment.soldiers.size(); ++soldierIndex)
            {
                const FSoldier& soldier = regiment.soldiers[soldierIndex];
                if (soldier.combatState == ESoldierState::Dying ||
                    soldier.combatState == ESoldierState::Dead)
                {
                    continue;
                }
                const int cell = CellIndex(CellX(soldier.position.x), CellZ(soldier.position.z));
                entries_.push_back({
                    {static_cast<int16_t>(regimentIndex), static_cast<int16_t>(soldierIndex)},
                    soldier.position,
                    heads_[cell]});
                heads_[cell] = static_cast<int>(entries_.size() - 1);
            }
        }
    }

    void FCombatGrid::Query(const glm::vec3& position, float radius,
                            std::vector<FCombatGridEntry>& results) const
    {
        results.clear();
        const int minX = CellX(position.x - radius);
        const int maxX = CellX(position.x + radius);
        const int minZ = CellZ(position.z - radius);
        const int maxZ = CellZ(position.z + radius);
        const float radiusSquared = radius * radius;
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                for (int entry = heads_[CellIndex(x, z)]; entry >= 0; entry = entries_[entry].next)
                {
                    const glm::vec2 delta(entries_[entry].position.x - position.x,
                                          entries_[entry].position.z - position.z);
                    if (glm::dot(delta, delta) <= radiusSquared)
                    {
                        results.push_back(entries_[entry].value);
                    }
                }
            }
        }
    }
}
