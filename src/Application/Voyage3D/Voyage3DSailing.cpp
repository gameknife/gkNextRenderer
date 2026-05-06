#include "Voyage3DSailing.hpp"

namespace
{
    constexpr float RotationSpeedRadSec = 1.5f;
    constexpr float AccelUnitsSec = 10.0f;
    constexpr float SpeedWorldScale = 0.65f;

    bool UsesCollision(const Voyage3D::FLandmassBlock& block)
    {
        return block.name == "iberia" ||
               block.name == "north_africa" ||
               block.name == "anatolia" ||
               block.name == "levant" ||
               block.name == "sicily";
    }

    bool IsNearPortChannel(const glm::vec3& worldPos, const std::vector<Voyage3D::FPortRuntime>& ports)
    {
        return std::any_of(ports.begin(), ports.end(), [&worldPos](const Voyage3D::FPortRuntime& port)
        {
            const glm::vec2 delta(worldPos.x - port.worldPos.x, worldPos.z - port.worldPos.z);
            return glm::length(delta) < 5.5f;
        });
    }
}

namespace Voyage3D
{
    bool IsInsideNavigableLand(const glm::vec3& worldPos,
                               const std::vector<FLandmassBlock>& landBlocks,
                               const std::vector<FPortRuntime>& ports)
    {
        if (IsNearPortChannel(worldPos, ports))
        {
            return false;
        }

        for (const FLandmassBlock& block : landBlocks)
        {
            if (!UsesCollision(block))
            {
                continue;
            }

            const glm::vec3 min = glm::min(block.min, block.max);
            const glm::vec3 max = glm::max(block.min, block.max);
            if (worldPos.x >= min.x && worldPos.x <= max.x &&
                worldPos.z >= min.z && worldPos.z <= max.z)
            {
                return true;
            }
        }
        return false;
    }

    void UpdatePlayerShip(FShipRuntime& ship,
                          double deltaSec,
                          const FInputState& input,
                          const std::vector<FLandmassBlock>& landBlocks,
                          const std::vector<FPortRuntime>& ports)
    {
        const float dt = static_cast<float>(deltaSec);
        ship.previousWorldPos = ship.worldPos;

        if (input.keyA)
        {
            ship.yaw += RotationSpeedRadSec * dt;
        }
        if (input.keyD)
        {
            ship.yaw -= RotationSpeedRadSec * dt;
        }

        const float debuffScale = input.stormDebuffMs > 0.0f ? 0.5f : 1.0f;
        const float maxSpeed = ship.def.speedKnots * SpeedWorldScale * debuffScale;
        if (input.keyW)
        {
            ship.currentSpeed = std::min(maxSpeed, ship.currentSpeed + AccelUnitsSec * dt);
        }
        if (input.keyS)
        {
            ship.currentSpeed = std::max(0.0f, ship.currentSpeed - AccelUnitsSec * dt);
        }
        if (!input.keyW && !input.keyS)
        {
            ship.currentSpeed = std::max(0.0f, ship.currentSpeed - AccelUnitsSec * 0.12f * dt);
        }
        ship.currentSpeed = std::min(ship.currentSpeed, maxSpeed);

        const glm::vec3 forward(std::cos(ship.yaw), 0.0f, std::sin(ship.yaw));
        ship.worldPos += forward * ship.currentSpeed * dt;
        ship.worldPos.x = std::clamp(ship.worldPos.x, -30.0f, 85.0f);
        ship.worldPos.z = std::clamp(ship.worldPos.z, -90.0f, 20.0f);

        if (IsInsideNavigableLand(ship.worldPos, landBlocks, ports))
        {
            ship.worldPos = ship.previousWorldPos;
            ship.currentSpeed *= 0.25f;
        }
    }
}
