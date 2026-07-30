#include "Battle/FormationLayout.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NextTotalwar::Formation
{
    namespace
    {
        int Files(int soldierCount, int ranks)
        {
            return std::max(1, (std::max(0, soldierCount) + std::max(1, ranks) - 1) / std::max(1, ranks));
        }
    }

    glm::vec2 SlotLocalOffset(int slotIndex, int soldierCount, int ranks,
                              float fileSpacing, float rankSpacing)
    {
        if (slotIndex < 0 || slotIndex >= soldierCount)
        {
            return {};
        }
        const int files = Files(soldierCount, ranks);
        const int row = slotIndex / files;
        const int column = slotIndex % files;
        const int rowStart = row * files;
        const int rowCount = std::min(files, soldierCount - rowStart);
        const float x = (static_cast<float>(column) - static_cast<float>(rowCount - 1) * 0.5f) * fileSpacing;
        return {x, -static_cast<float>(row) * rankSpacing};
    }

    glm::vec3 SlotWorld(const glm::vec3& anchor, float facing, const glm::vec2& localOffset)
    {
        const float sine = std::sin(facing);
        const float cosine = std::cos(facing);
        return anchor + glm::vec3(
            cosine * localOffset.x + sine * localOffset.y,
            0.0f,
            -sine * localOffset.x + cosine * localOffset.y);
    }

    glm::vec2 SlotLocal(const glm::vec3& anchor, float facing, const glm::vec3& worldPosition)
    {
        const float sine = std::sin(facing);
        const float cosine = std::cos(facing);
        const glm::vec3 offset = worldPosition - anchor;
        return {
            cosine * offset.x - sine * offset.z,
            sine * offset.x + cosine * offset.z};
    }

    glm::vec2 FormationHalfExtent(int soldierCount, int ranks, float fileSpacing, float rankSpacing)
    {
        const int files = Files(soldierCount, ranks);
        const int rows = std::max(1, (std::max(0, soldierCount) + files - 1) / files);
        return {
            static_cast<float>(files - 1) * fileSpacing * 0.5f,
            static_cast<float>(rows - 1) * rankSpacing * 0.5f};
    }

    std::vector<size_t> MinimumTravelAssignment(const std::vector<glm::vec3>& starts,
                                                const std::vector<glm::vec3>& destinations)
    {
        const size_t count = starts.size();
        if (count == 0 || destinations.size() != count)
        {
            return {};
        }

        // Hungarian algorithm: assignment[startIndex] = destinationIndex.
        // Iterating candidates in stable index order also makes equal-cost layouts deterministic.
        std::vector<double> rowPotential(count + 1, 0.0);
        std::vector<double> columnPotential(count + 1, 0.0);
        std::vector<size_t> matchedRow(count + 1, 0);
        std::vector<size_t> previousColumn(count + 1, 0);

        for (size_t row = 1; row <= count; ++row)
        {
            matchedRow[0] = row;
            size_t column0 = 0;
            std::vector<double> minimum(count + 1, std::numeric_limits<double>::max());
            std::vector<bool> used(count + 1, false);
            do
            {
                used[column0] = true;
                const size_t currentRow = matchedRow[column0];
                double delta = std::numeric_limits<double>::max();
                size_t column1 = 0;
                for (size_t column = 1; column <= count; ++column)
                {
                    if (used[column]) continue;
                    const glm::vec2 from(starts[currentRow - 1].x, starts[currentRow - 1].z);
                    const glm::vec2 to(destinations[column - 1].x, destinations[column - 1].z);
                    const double cost = static_cast<double>(glm::distance(from, to));
                    const double reducedCost =
                        cost - rowPotential[currentRow] - columnPotential[column];
                    if (reducedCost < minimum[column])
                    {
                        minimum[column] = reducedCost;
                        previousColumn[column] = column0;
                    }
                    if (minimum[column] < delta)
                    {
                        delta = minimum[column];
                        column1 = column;
                    }
                }
                for (size_t column = 0; column <= count; ++column)
                {
                    if (used[column])
                    {
                        rowPotential[matchedRow[column]] += delta;
                        columnPotential[column] -= delta;
                    }
                    else
                    {
                        minimum[column] -= delta;
                    }
                }
                column0 = column1;
            }
            while (matchedRow[column0] != 0);

            do
            {
                const size_t column1 = previousColumn[column0];
                matchedRow[column0] = matchedRow[column1];
                column0 = column1;
            }
            while (column0 != 0);
        }

        std::vector<size_t> assignment(count, 0);
        for (size_t column = 1; column <= count; ++column)
        {
            assignment[matchedRow[column] - 1] = column - 1;
        }
        return assignment;
    }

}
