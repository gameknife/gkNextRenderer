#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

#include <glm/glm.hpp>

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    enum class EDebrisKind : uint8_t
    {
        Tiny = 0,
        Chunk = 1,
        BossChunk = 2,
    };

    enum class EPickupState : uint8_t
    {
        None = 0,
        Physics = 1,
        Magnetic = 2,
    };

    enum class EDebrisPayload : uint8_t
    {
        None = 0,
        Material = 1,
        Xp = 2,
    };

    struct FDebrisRuntime
    {
        EDebrisKind kind = EDebrisKind::Tiny;
        NextBodyID bodyId{};
        std::shared_ptr<::Assets::Node> node;
        uint32_t baseModelId = 0;
        uint32_t currentMaterialId = 0;
        uint64_t activatedTickId = 0;
        bool active = false;
        EDebrisPayload payload = EDebrisPayload::None;
        EPickupState pickupState = EPickupState::None;
        int payloadValue = 0;
        float settleTimerMs = 0.0f;
        float magneticLerpProgress = 0.0f;
        glm::vec3 magneticPos = glm::vec3(0.0f);
        float remainingMs = 0.0f;
    };
}
