#include "Battle/FormationLayout.h"

#include <algorithm>
#include <cmath>

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

}
