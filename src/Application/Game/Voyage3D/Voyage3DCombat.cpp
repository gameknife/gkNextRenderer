#include "Voyage3DCombat.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Scene/NodeUtils.h"
#include "Voyage3DGameInstance.hpp"
#include "Voyage3DSailing.hpp"

namespace
{
    float NormalizeAngle(float angle)
    {
        while (angle > glm::pi<float>())
        {
            angle -= glm::two_pi<float>();
        }
        while (angle < -glm::pi<float>())
        {
            angle += glm::two_pi<float>();
        }
        return angle;
    }

    float RotateToward(float current, float target, float maxDelta)
    {
        const float delta = NormalizeAngle(target - current);
        return current + std::clamp(delta, -maxDelta, maxDelta);
    }

    glm::vec3 Forward(float yaw)
    {
        return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
    }

    glm::vec3 Side(float yaw, bool left)
    {
        const float angle = yaw + (left ? glm::half_pi<float>() : -glm::half_pi<float>());
        return glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
    }

    void HideProjectile(Voyage3D::FProjectileRuntime& projectile)
    {
        projectile.active = false;
        projectile.lifetimeMs = 0.0f;
        if (projectile.node)
        {
            projectile.node->SetTranslation(Voyage3D::HiddenPosition);
            Assets::NodeUtils::SetVisible(projectile.node, false);
        }
    }

    void UpdateCooldowns(Voyage3D::FShipRuntime& ship, float deltaMs)
    {
        ship.leftBroadsideCooldownMs = std::max(0.0f, ship.leftBroadsideCooldownMs - deltaMs);
        ship.rightBroadsideCooldownMs = std::max(0.0f, ship.rightBroadsideCooldownMs - deltaMs);
        ship.aiFireCooldownMs = std::max(0.0f, ship.aiFireCooldownMs - deltaMs);
    }
}

namespace Voyage3D
{
    void UpdateCombat(Voyage3DGameInstance& gameInstance, double deltaSec)
    {
        const float dt = static_cast<float>(deltaSec);
        const float deltaMs = dt * 1000.0f;
        FShipRuntime& player = gameInstance.GetPlayerShip();
        UpdateCooldowns(player, deltaMs);

        UpdatePlayerShip(player, deltaSec, gameInstance.GetInputState(), gameInstance.GetLandBlocks(), gameInstance.GetPorts());
        gameInstance.UpdateShipNodeTransform(player);

        for (FShipRuntime& enemy : gameInstance.GetEnemyShips())
        {
            if (!enemy.active)
            {
                continue;
            }

            UpdateCooldowns(enemy, deltaMs);
            glm::vec3 toPlayer = player.worldPos - enemy.worldPos;
            toPlayer.y = 0.0f;
            const float distance = glm::length(toPlayer);
            if (distance > 0.001f)
            {
                const glm::vec3 dir = toPlayer / distance;
                const float toPlayerYaw = std::atan2(dir.z, dir.x);
                const float leftDot = glm::dot(Side(enemy.yaw, true), dir);
                const float rightDot = glm::dot(Side(enemy.yaw, false), dir);
                const bool useLeft = std::abs(leftDot) >= std::abs(rightDot);
                const float desiredYaw = NormalizeAngle(toPlayerYaw + (useLeft ? -glm::half_pi<float>() : glm::half_pi<float>()));
                enemy.yaw = RotateToward(enemy.yaw, desiredYaw, 1.8f * dt);

                const float damageSpeedScale = enemy.currentHp < enemy.def.hp * 0.3f ? 0.5f : 1.0f;
                const float maxSpeed = enemy.def.speedKnots * 0.48f * damageSpeedScale;
                if (distance > 4.8f)
                {
                    enemy.currentSpeed = std::min(maxSpeed, enemy.currentSpeed + 8.0f * dt);
                }
                else
                {
                    enemy.currentSpeed = std::max(0.0f, enemy.currentSpeed - 10.0f * dt);
                }
                enemy.previousWorldPos = enemy.worldPos;
                enemy.worldPos += Forward(enemy.yaw) * enemy.currentSpeed * dt;
                enemy.worldPos.x = std::clamp(enemy.worldPos.x, -30.0f, 85.0f);
                enemy.worldPos.z = std::clamp(enemy.worldPos.z, -90.0f, 20.0f);

                const glm::vec3 chosenSide = Side(enemy.yaw, useLeft);
                if (distance < 6.0f && glm::dot(chosenSide, dir) > std::cos(glm::radians(15.0f)) && enemy.aiFireCooldownMs <= 0.0f)
                {
                    gameInstance.FireBroadside(enemy, useLeft, false);
                    enemy.aiFireCooldownMs = 1800.0f;
                }
            }
            gameInstance.UpdateShipNodeTransform(enemy);
        }

        for (FProjectileRuntime& projectile : gameInstance.GetProjectiles())
        {
            if (!projectile.active)
            {
                continue;
            }

            projectile.worldPos += projectile.velocity * dt;
            projectile.lifetimeMs -= deltaMs;
            if (projectile.node)
            {
                projectile.node->SetTranslation(projectile.worldPos);
            }
            if (projectile.lifetimeMs <= 0.0f)
            {
                HideProjectile(projectile);
                continue;
            }

            if (projectile.fromPlayer)
            {
                for (FShipRuntime& enemy : gameInstance.GetEnemyShips())
                {
                    if (!enemy.active)
                    {
                        continue;
                    }
                    if (glm::length(projectile.worldPos - enemy.worldPos) < 1.0f)
                    {
                        enemy.currentHp = std::max(0, enemy.currentHp - projectile.damage);
                        gameInstance.PushFloatingText(enemy.worldPos + glm::vec3(0.0f, 1.8f, 0.0f),
                                                      fmt::format("-{}", projectile.damage),
                                                      glm::vec4(1.0f, 0.20f, 0.12f, 1.0f),
                                                      850.0f,
                                                      1.2f);
                        gameInstance.StartScreenShake(120.0f, 1.5f);
                        HideProjectile(projectile);
                        if (enemy.currentHp <= 0)
                        {
                            gameInstance.PushExplosionRing(enemy.worldPos, glm::vec4(1.0f, 0.45f, 0.10f, 1.0f), 4.0f);
                            gameInstance.StartScreenShake(260.0f, 2.5f);
                            gameInstance.EndCombatVictory();
                            return;
                        }
                        break;
                    }
                }
            }
            else if (glm::length(projectile.worldPos - player.worldPos) < 1.0f)
            {
                gameInstance.DamagePlayer(projectile.damage);
                HideProjectile(projectile);
                if (player.currentHp <= 0)
                {
                    gameInstance.EndCombatDefeat();
                    return;
                }
            }
        }
    }
}
