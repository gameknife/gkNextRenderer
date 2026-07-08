#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Rendering/AuxDraw/AuxDrawSystem.hpp"

#include <algorithm>

namespace Rendering::AuxDraw
{
    namespace
    {
        constexpr float minPrimitiveSize = 1.0f;

        uint32_t MakeFlags(bool depthTest)
        {
            return depthTest ? AuxPrimitiveDepthTest : 0u;
        }
    }

    FAuxPrimitiveGpu FAuxDrawSystem::MakeLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest)
    {
        FAuxPrimitiveGpu primitive{};
        primitive.p0Type = glm::vec4(from, 0.0f);
        primitive.p1Size = glm::vec4(to, std::max(size, minPrimitiveSize));
        primitive.color = color;
        primitive.params = glm::uvec4(static_cast<uint32_t>(EAuxPrimitiveType::Line), MakeFlags(depthTest), 0u, 0u);
        return primitive;
    }

    FAuxPrimitiveGpu FAuxDrawSystem::MakePoint(glm::vec3 location, glm::vec4 color, float size, bool depthTest)
    {
        FAuxPrimitiveGpu primitive{};
        primitive.p0Type = glm::vec4(location, 1.0f);
        primitive.p1Size = glm::vec4(location, std::max(size, minPrimitiveSize));
        primitive.color = color;
        primitive.params = glm::uvec4(static_cast<uint32_t>(EAuxPrimitiveType::Point), MakeFlags(depthTest), 0u, 0u);
        return primitive;
    }

    void FAuxDrawSystem::AddLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest)
    {
        transientPrimitives_.push_back(MakeLine(from, to, color, size, depthTest));
    }

    void FAuxDrawSystem::AddBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size, bool depthTest)
    {
        AddLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z), color, size, depthTest);
        AddLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, min.y, min.z), color, size, depthTest);

        AddLine(glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, min.y, max.z), color, size, depthTest);

        AddLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color, size, depthTest);
        AddLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, max.y, max.z), color, size, depthTest);
    }

    void FAuxDrawSystem::AddPoint(glm::vec3 location, glm::vec4 color, float size, int32_t durationInTick,
                                  bool depthTest)
    {
        const FAuxPrimitiveGpu primitive = MakePoint(location, color, size, depthTest);
        if (durationInTick > 0)
        {
            persistentPrimitives_.push_back(FPersistentPrimitive{primitive, durationInTick});
            return;
        }
        transientPrimitives_.push_back(primitive);
    }

    void FAuxDrawSystem::ConsumeFramePrimitives(
        std::vector<FAuxPrimitiveGpu>& outPrimitives,
        uint32_t maxPrimitiveCount)
    {
        outPrimitives.clear();
        outPrimitives.reserve(std::min<size_t>(
            static_cast<size_t>(maxPrimitiveCount),
            transientPrimitives_.size() + persistentPrimitives_.size()));

        uint32_t overflowCount = 0;
        auto appendPrimitive = [&](const FAuxPrimitiveGpu& primitive)
        {
            if (outPrimitives.size() < maxPrimitiveCount)
            {
                outPrimitives.push_back(primitive);
                return;
            }
            ++overflowCount;
        };

        for (const FPersistentPrimitive& item : persistentPrimitives_)
        {
            appendPrimitive(item.primitive);
        }
        for (const FAuxPrimitiveGpu& primitive : transientPrimitives_)
        {
            appendPrimitive(primitive);
        }

        transientPrimitives_.clear();
        for (FPersistentPrimitive& item : persistentPrimitives_)
        {
            --item.remainingTicks;
        }
        persistentPrimitives_.erase(
            std::remove_if(
                persistentPrimitives_.begin(),
                persistentPrimitives_.end(),
                [](const FPersistentPrimitive& item)
                {
                    return item.remainingTicks <= 0;
                }),
            persistentPrimitives_.end());

        lastOverflowCount_ = overflowCount;
    }

    std::vector<FAuxPrimitiveGpu> FAuxDrawSystem::ConsumeFramePrimitives(uint32_t maxPrimitiveCount)
    {
        std::vector<FAuxPrimitiveGpu> primitives;
        ConsumeFramePrimitives(primitives, maxPrimitiveCount);
        return primitives;
    }

    FAuxDrawSystem& GetAuxDrawSystem()
    {
        static FAuxDrawSystem system;
        return system;
    }
}
