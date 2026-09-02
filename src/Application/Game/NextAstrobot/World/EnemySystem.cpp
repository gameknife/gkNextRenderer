#include "Application/Game/NextAstrobot/World/EnemySystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    namespace
    {
        void SetEnemyVisible(Assets::Node* node, bool visible)
        {
            if (!node)
            {
                return;
            }
            if (auto* render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(visible);
            }
        }

        float EnemyHeight(EEnemyKind kind)
        {
            switch (kind)
            {
            case EEnemyKind::Walker: return 1.42f;
            case EEnemyKind::Flyer: return 1.0f;
            case EEnemyKind::Spiky: return 1.16f;
            }
            return 1.2f;
        }
    }

    void FEnemySystem::Unbind()
    {
        enemies_.clear();
    }

    void FEnemySystem::Bind(const FLevelIndex& index)
    {
        Unbind();
        enemies_.reserve(index.enemies.size());
        for (size_t i = 0; i < index.enemies.size(); ++i)
        {
            const FTypedNode& entry = index.enemies[i];
            FEnemy enemy;
            enemy.node = entry.node.node;
            enemy.kind = static_cast<EEnemyKind>(entry.kind);
            enemy.origin = entry.node.worldPos;
            enemy.position = entry.node.worldPos;
            enemy.height = EnemyHeight(enemy.kind);
            // A deterministic per-enemy offset keeps a row of patrols from marching in step.
            enemy.phaseOffset = static_cast<float>(i) * 0.37f;
            if (enemy.node)
            {
                enemy.bindRotation = enemy.node->Rotation();
                enemy.bindTranslation = enemy.node->Translation();
            }
            enemies_.push_back(enemy);
        }
    }

    void FEnemySystem::ResetAll()
    {
        for (FEnemy& enemy : enemies_)
        {
            enemy.alive = true;
            enemy.knockback = 0.0f;
            enemy.position = enemy.origin;
            SetEnemyVisible(enemy.node, true);
        }
    }

    int FEnemySystem::AliveCount() const
    {
        return static_cast<int>(std::count_if(enemies_.begin(), enemies_.end(),
                                              [](const FEnemy& enemy) { return enemy.alive; }));
    }

    FEnemyOutcome FEnemySystem::Update(float time, float deltaSeconds, const glm::vec3& playerFoot,
                                       float playerHeight, float playerFallSpeed, bool punchActive,
                                       const glm::vec3& punchOrigin, const glm::vec3& punchDirection,
                                       float punchRange, float punchArcDegrees)
    {
        FEnemyOutcome outcome;
        const float punchCos = std::cos(glm::radians(punchArcDegrees * 0.5f));
        const glm::vec3 punchForward = glm::length(punchDirection) > 0.001f ? glm::normalize(punchDirection)
                                                                           : glm::vec3(0.0f, 0.0f, 1.0f);

        for (FEnemy& enemy : enemies_)
        {
            if (!enemy.alive || !enemy.node)
            {
                continue;
            }

            // --- patrol ---
            glm::vec3 offset(0.0f);
            switch (enemy.kind)
            {
            case EEnemyKind::Walker:
            case EEnemyKind::Spiky:
            {
                const float half = config_.EnemyPatrolHalfLength;
                const float period = (4.0f * half) / std::max(config_.EnemyPatrolSpeed, 0.05f);
                const float t = PingPong01(time + enemy.phaseOffset * period, period);
                // Patrol runs along the module's own facing (SCAD -y = engine +z).
                offset = enemy.bindRotation * glm::vec3(0.0f, 0.0f, (t * 2.0f - 1.0f) * half);
                break;
            }
            case EEnemyKind::Flyer:
            {
                const float angle = (time + enemy.phaseOffset) * config_.FlyerOrbitSpeed;
                offset = glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * config_.FlyerOrbitRadius;
                break;
            }
            }

            enemy.knockback = Approach(enemy.knockback, 0.0f, 4.0f, deltaSeconds);
            enemy.position = enemy.origin + offset + punchForward * enemy.knockback;
            enemy.node->SetTranslation(enemy.bindTranslation + glm::inverse(enemy.bindRotation) *
                                                                   (enemy.position - enemy.origin));

            // --- punch ---
            if (punchActive)
            {
                // Horizontal sector, like InteractableSystem: a flyer overhead is still in
                // front of the player even though the 3D direction points up.
                const glm::vec3 toEnemy = enemy.position + glm::vec3(0.0f, enemy.height * 0.5f, 0.0f) - punchOrigin;
                const glm::vec3 flat(toEnemy.x, 0.0f, toEnemy.z);
                const float flatLength = glm::length(flat);
                if (glm::length(toEnemy) < punchRange + 0.5f &&
                    (flatLength < 0.05f || glm::dot(flat / flatLength, punchForward) >= punchCos))
                {
                    ++outcome.punchedCount;
                    if (enemy.kind == EEnemyKind::Spiky)
                    {
                        // Armoured: a punch only shoves it away.
                        enemy.knockback = 1.2f;
                    }
                    else
                    {
                        enemy.alive = false;
                        SetEnemyVisible(enemy.node, false);
                        continue;
                    }
                }
            }

            // --- contact ---
            const glm::vec3 delta = playerFoot - enemy.position;
            const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
            if (horizontal > config_.EnemyRadius)
            {
                continue;
            }
            const bool aboveHead = playerFoot.y >= enemy.position.y + enemy.height - config_.StompMargin;
            if (aboveHead && playerFallSpeed < -0.5f && enemy.kind != EEnemyKind::Spiky)
            {
                enemy.alive = false;
                SetEnemyVisible(enemy.node, false);
                outcome.stomped = true;
                continue;
            }
            if (playerFoot.y < enemy.position.y + enemy.height && playerFoot.y + playerHeight > enemy.position.y)
            {
                outcome.killedPlayer = true;
            }
        }
        return outcome;
    }
}
