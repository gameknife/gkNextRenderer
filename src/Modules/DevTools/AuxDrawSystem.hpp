#pragma once

#include "Engine/Runtime/Interface/DebugDraw.hpp"
#include <glm/vec4.hpp>

namespace DevTools
{
    enum class EAuxPrimitiveType : uint32_t { Line, Point };
    enum EAuxPrimitiveFlags : uint32_t { AuxPrimitiveDepthTest = 1u << 0u };

    struct FAuxPrimitiveGpu
    {
        glm::vec4 p0Type{0.0f};
        glm::vec4 p1Size{0.0f};
        glm::vec4 color{1.0f};
        glm::uvec4 params{0u};
    };
    static_assert(sizeof(FAuxPrimitiveGpu) == 64);

    class FAuxDrawSystem final : public Runtime::IDebugDraw
    {
    public:
        void AddLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest) override;
        void AddBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size, bool depthTest) override;
        void AddPoint(glm::vec3 location, glm::vec4 color, float size, int32_t durationInTick, bool depthTest) override;
        void ConsumeFramePrimitives(std::vector<FAuxPrimitiveGpu>& outPrimitives, uint32_t maxPrimitiveCount);
        uint32_t LastOverflowCount() const { return lastOverflowCount_; }

    private:
        struct FPersistentPrimitive { FAuxPrimitiveGpu primitive; int32_t remainingTicks = 0; };
        static FAuxPrimitiveGpu MakeLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest);
        static FAuxPrimitiveGpu MakePoint(glm::vec3 location, glm::vec4 color, float size, bool depthTest);
        std::vector<FAuxPrimitiveGpu> transientPrimitives_;
        std::vector<FPersistentPrimitive> persistentPrimitives_;
        uint32_t lastOverflowCount_ = 0;
    };
}
