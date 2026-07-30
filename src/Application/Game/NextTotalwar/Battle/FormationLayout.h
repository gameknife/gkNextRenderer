#pragma once

#include "NextTotalwarTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace NextTotalwar::Formation
{
    glm::vec2 SlotLocalOffset(int slotIndex, int soldierCount, int ranks,
                              float fileSpacing, float rankSpacing);
    glm::vec3 SlotWorld(const glm::vec3& anchor, float facing, const glm::vec2& localOffset);
    glm::vec2 SlotLocal(const glm::vec3& anchor, float facing, const glm::vec3& worldPosition);
    glm::vec2 FormationHalfExtent(int soldierCount, int ranks, float fileSpacing, float rankSpacing);
    void RepackSlots(FRegiment& regiment);
    void PrepareNearestReform(FRegiment& regiment);
    std::vector<size_t> MinimumTravelAssignment(const std::vector<glm::vec3>& starts,
                                                const std::vector<glm::vec3>& destinations);
}
