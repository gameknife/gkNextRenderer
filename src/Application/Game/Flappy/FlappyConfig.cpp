#include "Application/Game/Flappy/FlappyConfig.hpp"

#include "Runtime/Utilities/JsonHelpers.h"

#include <nlohmann/json.hpp>

namespace
{
    glm::vec3 GetVec3OrDefault(const nlohmann::json& object, const char* key, const glm::vec3& fallback)
    {
        return NextJson::GetVec3(object, key, fallback);
    }

    template <typename TValue>
    TValue GetOptionalObjectValue(const nlohmann::json& parent,
                                  const char* objectName,
                                  const char* key,
                                  const TValue& fallback)
    {
        if (!parent.contains(objectName) || !parent.at(objectName).is_object())
        {
            return fallback;
        }
        return NextJson::GetOptional<TValue>(parent.at(objectName), key, fallback);
    }
}

namespace Flappy
{
    const char* ToString(EGameState state)
    {
        switch (state)
        {
        case EGameState::Ready:
            return "Ready";
        case EGameState::Playing:
            return "Playing";
        case EGameState::Dead:
            return "Dead";
        default:
            return "Unknown";
        }
    }

    FGameplayConfig LoadGameplayConfig(const std::string& path)
    {
        const nlohmann::json document = NextJson::LoadFile(path);
        FGameplayConfig config{};

        if (document.contains("camera") && document.at("camera").is_object())
        {
            const nlohmann::json& camera = document.at("camera");
            config.camera.position = GetVec3OrDefault(camera, "position", config.camera.position);
            config.camera.target = GetVec3OrDefault(camera, "target", config.camera.target);
            config.camera.up = GetVec3OrDefault(camera, "up", config.camera.up);
            config.camera.fieldOfView = NextJson::GetOptional<float>(camera, "fieldOfView", config.camera.fieldOfView);
        }

        if (document.contains("world") && document.at("world").is_object())
        {
            const nlohmann::json& world = document.at("world");
            config.world.minX = NextJson::GetOptional<float>(world, "minX", config.world.minX);
            config.world.maxX = NextJson::GetOptional<float>(world, "maxX", config.world.maxX);
            config.world.minY = NextJson::GetOptional<float>(world, "minY", config.world.minY);
            config.world.maxY = NextJson::GetOptional<float>(world, "maxY", config.world.maxY);
            config.world.gameplayZ = NextJson::GetOptional<float>(world, "gameplayZ", config.world.gameplayZ);
        }

        if (document.contains("bird") && document.at("bird").is_object())
        {
            const nlohmann::json& bird = document.at("bird");
            config.bird.initialPosition = GetVec3OrDefault(bird, "initialPosition", config.bird.initialPosition);
            config.bird.radius = NextJson::GetOptional<float>(bird, "radius", config.bird.radius);
            config.bird.gravity = NextJson::GetOptional<float>(bird, "gravity", config.bird.gravity);
            config.bird.flapVelocity = NextJson::GetOptional<float>(bird, "flapVelocity", config.bird.flapVelocity);
            config.bird.minVelocity = NextJson::GetOptional<float>(bird, "minVelocity", config.bird.minVelocity);
            config.bird.maxVelocity = NextJson::GetOptional<float>(bird, "maxVelocity", config.bird.maxVelocity);
        }

        if (document.contains("pipe") && document.at("pipe").is_object())
        {
            const nlohmann::json& pipe = document.at("pipe");
            config.pipe.width = NextJson::GetOptional<float>(pipe, "width", config.pipe.width);
            config.pipe.gapHeight = NextJson::GetOptional<float>(pipe, "gapHeight", config.pipe.gapHeight);
            config.pipe.gapCenterMinY = NextJson::GetOptional<float>(pipe, "gapCenterMinY", config.pipe.gapCenterMinY);
            config.pipe.gapCenterMaxY = NextJson::GetOptional<float>(pipe, "gapCenterMaxY", config.pipe.gapCenterMaxY);
            config.pipe.spawnInterval = NextJson::GetOptional<float>(pipe, "spawnInterval", config.pipe.spawnInterval);
            config.pipe.spawnX = NextJson::GetOptional<float>(pipe, "spawnX", config.pipe.spawnX);
            config.pipe.destroyX = NextJson::GetOptional<float>(pipe, "destroyX", config.pipe.destroyX);
            config.pipe.speed = NextJson::GetOptional<float>(pipe, "speed", config.pipe.speed);
            config.pipe.poolSize = NextJson::GetOptional<int>(pipe, "poolSize", config.pipe.poolSize);
        }

        config.fixedDeltaSeconds =
            NextJson::GetOptional<float>(document, "fixedDeltaSeconds", config.fixedDeltaSeconds);
        config.deadHitStopSeconds =
            NextJson::GetOptional<float>(document, "deadHitStopSeconds", config.deadHitStopSeconds);
        config.rngSeed = GetOptionalObjectValue<uint32_t>(document, "determinism", "rngSeed", config.rngSeed);

        return config;
    }

    FReplayConfig LoadReplayConfig(const std::string& path)
    {
        const nlohmann::json document = NextJson::LoadFile(path);
        FReplayConfig config{};
        config.maxFrames = NextJson::GetOptional<int>(document, "maxFrames", config.maxFrames);
        if (document.contains("flapFrames") && document.at("flapFrames").is_array())
        {
            config.flapFrames = document.at("flapFrames").get<std::vector<int>>();
        }
        return config;
    }
}
