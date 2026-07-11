#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/AuxDrawSystem.hpp"

#include <algorithm>

namespace DevTools
{
    namespace
    {
        constexpr float minPrimitiveSize = 1.0f;
        uint32_t MakeFlags(bool depthTest) { return depthTest ? AuxPrimitiveDepthTest : 0u; }
    }

    FAuxPrimitiveGpu FAuxDrawSystem::MakeLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest)
    {
        return {glm::vec4(from, 0.0f), glm::vec4(to, std::max(size, minPrimitiveSize)), color,
                glm::uvec4(static_cast<uint32_t>(EAuxPrimitiveType::Line), MakeFlags(depthTest), 0u, 0u)};
    }

    FAuxPrimitiveGpu FAuxDrawSystem::MakePoint(glm::vec3 location, glm::vec4 color, float size, bool depthTest)
    {
        return {glm::vec4(location, 1.0f), glm::vec4(location, std::max(size, minPrimitiveSize)), color,
                glm::uvec4(static_cast<uint32_t>(EAuxPrimitiveType::Point), MakeFlags(depthTest), 0u, 0u)};
    }

    void FAuxDrawSystem::AddLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest)
    {
        transientPrimitives_.push_back(MakeLine(from, to, color, size, depthTest));
    }

    void FAuxDrawSystem::AddBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size, bool depthTest)
    {
        const glm::vec3 corners[]{
            {min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z}};
        constexpr uint32_t edges[][2]{{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                      {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : edges) AddLine(corners[edge[0]], corners[edge[1]], color, size, depthTest);
    }

    void FAuxDrawSystem::AddPoint(glm::vec3 location, glm::vec4 color, float size,
                                  int32_t durationInTick, bool depthTest)
    {
        const FAuxPrimitiveGpu primitive = MakePoint(location, color, size, depthTest);
        if (durationInTick > 0) persistentPrimitives_.push_back({primitive, durationInTick});
        else transientPrimitives_.push_back(primitive);
    }

    void FAuxDrawSystem::ConsumeFramePrimitives(std::vector<FAuxPrimitiveGpu>& output, uint32_t maximum)
    {
        output.clear();
        output.reserve(std::min<size_t>(maximum, transientPrimitives_.size() + persistentPrimitives_.size()));
        uint32_t overflow = 0;
        auto append = [&](const FAuxPrimitiveGpu& primitive)
        {
            if (output.size() < maximum) output.push_back(primitive); else ++overflow;
        };
        for (const auto& item : persistentPrimitives_) append(item.primitive);
        for (const auto& primitive : transientPrimitives_) append(primitive);
        transientPrimitives_.clear();
        for (auto& item : persistentPrimitives_) --item.remainingTicks;
        std::erase_if(persistentPrimitives_, [](const auto& item) { return item.remainingTicks <= 0; });
        lastOverflowCount_ = overflow;
    }
}
