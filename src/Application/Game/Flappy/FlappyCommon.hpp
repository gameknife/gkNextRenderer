#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

namespace Flappy
{
    enum class EGameState : uint8_t
    {
        Ready,
        Playing,
        Dead,
    };

    const char* ToString(EGameState state);

    struct FCameraConfig
    {
        glm::vec3 position{0.0f, 0.0f, 12.0f};
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float fieldOfView = 50.0f;
    };

    struct FWorldConfig
    {
        float minX = -10.0f;
        float maxX = 10.0f;
        float minY = -5.6f;
        float maxY = 5.6f;
        float gameplayZ = 0.0f;
    };

    struct FBirdConfig
    {
        glm::vec3 initialPosition{-3.0f, 0.0f, 0.0f};
        float radius = 0.4f;
        float gravity = -22.0f;
        float flapVelocity = 7.5f;
        float minVelocity = -10.0f;
        float maxVelocity = 10.0f;
    };

    struct FPipeConfig
    {
        float width = 1.0f;
        float gapHeight = 2.6f;
        float gapCenterMinY = -2.5f;
        float gapCenterMaxY = 2.5f;
        float spawnInterval = 1.6f;
        float spawnX = 12.0f;
        float destroyX = -12.0f;
        float speed = 3.0f;
        int poolSize = 12;
    };

    struct FGameplayConfig
    {
        FCameraConfig camera;
        FWorldConfig world;
        FBirdConfig bird;
        FPipeConfig pipe;
        float fixedDeltaSeconds = 1.0f / 60.0f;
        float deadHitStopSeconds = 0.5f;
        uint32_t rngSeed = 0x00C0FFEEu;
    };

    struct FReplayConfig
    {
        int maxFrames = 600;
        std::vector<int> flapFrames;
    };
}
