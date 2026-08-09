#include "FlappyConfig.hpp"

#include "Engine/Runtime/Utilities/JsonHelpers.hpp"

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
            config.world.backdropZ = NextJson::GetOptional<float>(world, "backdropZ", config.world.backdropZ);
        }

        if (document.contains("environment") && document.at("environment").is_object())
        {
            const nlohmann::json& environment = document.at("environment");
            config.environment.skyIndex =
                NextJson::GetOptional<int>(environment, "skyIndex", config.environment.skyIndex);
            config.environment.skyIntensity =
                NextJson::GetOptional<float>(environment, "skyIntensity", config.environment.skyIntensity);
            config.environment.sunIntensity =
                NextJson::GetOptional<float>(environment, "sunIntensity", config.environment.sunIntensity);
            config.environment.sunRotation =
                NextJson::GetOptional<float>(environment, "sunRotation", config.environment.sunRotation);
            config.environment.sunElevation =
                NextJson::GetOptional<float>(environment, "sunElevation", config.environment.sunElevation);
        }

        if (document.contains("parallax") && document.at("parallax").is_object())
        {
            const nlohmann::json& parallax = document.at("parallax");
            config.parallax.mountainZ =
                NextJson::GetOptional<float>(parallax, "mountainZ", config.parallax.mountainZ);
            config.parallax.mountainSpeed =
                NextJson::GetOptional<float>(parallax, "mountainSpeed", config.parallax.mountainSpeed);
            config.parallax.mountainSpacing =
                NextJson::GetOptional<float>(parallax, "mountainSpacing", config.parallax.mountainSpacing);
            config.parallax.mountainCount =
                NextJson::GetOptional<int>(parallax, "mountainCount", config.parallax.mountainCount);
            config.parallax.vegetationZ =
                NextJson::GetOptional<float>(parallax, "vegetationZ", config.parallax.vegetationZ);
            config.parallax.vegetationSpeed =
                NextJson::GetOptional<float>(parallax, "vegetationSpeed", config.parallax.vegetationSpeed);
            config.parallax.vegetationSpacing =
                NextJson::GetOptional<float>(parallax, "vegetationSpacing", config.parallax.vegetationSpacing);
            config.parallax.vegetationCount =
                NextJson::GetOptional<int>(parallax, "vegetationCount", config.parallax.vegetationCount);
            config.parallax.cloudZ =
                NextJson::GetOptional<float>(parallax, "cloudZ", config.parallax.cloudZ);
            config.parallax.cloudSpeed =
                NextJson::GetOptional<float>(parallax, "cloudSpeed", config.parallax.cloudSpeed);
            config.parallax.cloudSpacing =
                NextJson::GetOptional<float>(parallax, "cloudSpacing", config.parallax.cloudSpacing);
            config.parallax.cloudCount =
                NextJson::GetOptional<int>(parallax, "cloudCount", config.parallax.cloudCount);
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
