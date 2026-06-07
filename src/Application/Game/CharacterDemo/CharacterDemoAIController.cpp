#include "CharacterDemoAIController.hpp"

#include "Engine/NextGameplay/Gameplay/GameplayMath.hpp"

#include <cassert>

using NextGameplay::AdvanceYawToward;
using NextGameplay::NormalizeHorizontalOrZero;

namespace
{
    constexpr std::array<const char*, 6> PatrolNodeNames{
        "WarmupPad_Left",
        "WarmupPad_Right",
        "Connector_LeftBranch",
        "Connector_RightBranch",
        "Connector_BackLeft",
        "Connector_BackRight",
    };
}

void CharacterDemoAIController::Bind(NextGameplay::CharacterActor* aiCharacter,
                                     NextGameplay::AIAgentComponent* aiAgent,
                                     const NextGameplay::CharacterActor* playerCharacter,
                                     NextGameplay::FNavGrid* navGrid,
                                     std::mt19937* patrolRng,
                                     FCallbacks callbacks)
{
    aiCharacter_ = aiCharacter;
    aiAgent_ = aiAgent;
    playerCharacter_ = playerCharacter;
    navGrid_ = navGrid;
    patrolRng_ = patrolRng;
    callbacks_ = std::move(callbacks);
}

void CharacterDemoAIController::Reset()
{
    aiCharacter_ = nullptr;
    aiAgent_ = nullptr;
    playerCharacter_ = nullptr;
    navGrid_ = nullptr;
    patrolRng_ = nullptr;
    callbacks_ = {};
}

void CharacterDemoAIController::OnNavGridRebuilt()
{
    if (aiAgent_)
    {
        aiAgent_->pathFollower.Clear();
    }
}

void CharacterDemoAIController::CollectPatrolPoints()
{
    auto& agent = GetAIAgent();
    agent.patrolPoints.clear();

    for (const char* nodeName : PatrolNodeNames)
    {
        glm::vec3 position(0.0f);
        if (!callbacks_.tryGetSceneNodePosition || !callbacks_.tryGetSceneNodePosition(nodeName, position))
        {
            continue;
        }

        bool duplicate = false;
        for (const glm::vec3& existing : agent.patrolPoints)
        {
            if (glm::distance(existing, position) < 0.5f)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
        {
            agent.patrolPoints.push_back(position);
        }
    }

    if (agent.patrolPoints.empty())
    {
        agent.patrolPoints = {
            glm::vec3(-6.0f, 0.0f, 6.0f),
            glm::vec3(6.0f, 0.0f, 6.0f),
            glm::vec3(-12.0f, 0.0f, 18.0f),
            glm::vec3(12.0f, 0.0f, 18.0f),
        };
    }
}

void CharacterDemoAIController::Update(float deltaSeconds, const CharacterDemoAIConfig& config)
{
    if (!aiAgent_)
    {
        return;
    }

    auto& agent = GetAIAgent();
    auto& aiCharacter = GetAICharacter();

    if (!aiCharacter.controller.IsValid())
    {
        agent.ResetDebugState();
        return;
    }

    agent.fireCooldownRemaining = std::max(0.0f, agent.fireCooldownRemaining - deltaSeconds);
    agent.targetMemoryRemaining = std::max(0.0f, agent.targetMemoryRemaining - deltaSeconds);
    agent.patrolPauseRemaining = std::max(0.0f, agent.patrolPauseRemaining - deltaSeconds);
    agent.targetVisibleGraceRemaining = std::max(0.0f, agent.targetVisibleGraceRemaining - deltaSeconds);
    agent.stateHoldRemaining = std::max(0.0f, agent.stateHoldRemaining - deltaSeconds);
    agent.moveDir = glm::vec3(0.0f);
    agent.triggerJump = false;
    aiCharacter.ClearControlFrameState();
    aiCharacter.SetControlIntent(glm::vec3(0.0f), agent.lookDir, 0.0f, false, false);

    const glm::vec3 aiPos = aiCharacter.controller.GetPosition();
    const glm::vec3 playerPos = GetPlayerCharacter().controller.GetPosition();
    const glm::vec3 toPlayer = playerPos - aiPos;
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
    const glm::vec3 botForward(std::sin(agent.yaw), 0.0f, std::cos(agent.yaw));
    const float fovDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool closeThreat = distanceToPlayer <= config.PreferredCombatRangeMin;
    const bool hasLineOfSight = closeThreat || (callbacks_.hasLineOfSightToPlayer && callbacks_.hasLineOfSightToPlayer());
    const float sightRange = agent.GetTargetVisible() ? config.LoseSightRange : config.SightRange;
    const bool rawTargetVisible =
        config.Enabled &&
        distanceToPlayer <= sightRange &&
        std::abs(toPlayer.y) <= 4.0f &&
        hasLineOfSight &&
        (closeThreat || fovDot >= std::cos(glm::radians(75.0f)));

    if (rawTargetVisible)
    {
        agent.lastKnownTargetPosition = playerPos;
        agent.targetMemoryRemaining = config.MemoryTime;
        agent.targetVisibleGraceRemaining = config.TargetVisibleGraceTime;
    }

    agent.SetEnabled(config.Enabled);
    agent.SetTargetVisible(rawTargetVisible || agent.targetVisibleGraceRemaining > 0.0f);

    if (!config.Enabled)
    {
        agent.SetState(EAIBotState::Disabled);
        agent.SetDesiredState(EAIBotState::Disabled);
        agent.ResetDebugState();
        aiCharacter.SetControlIntent(glm::vec3(0.0f), agent.lookDir, 0.0f, false, false);
        aiCharacter.controller.Update(aiCharacter.control ? aiCharacter.control->GetMoveIntent() : glm::vec3(0.0f),
                                      aiCharacter.control ? aiCharacter.control->GetDesiredSpeed() : 0.0f,
                                      aiCharacter.ConsumeJumpRequested(),
                                      deltaSeconds);
        if (callbacks_.updateAnimationState)
        {
            callbacks_.updateAnimationState(deltaSeconds);
        }
        if (callbacks_.updateNode)
        {
            callbacks_.updateNode();
        }
        return;
    }

    agent.SetPreviousState(agent.GetState());
    auto getStatePriority = [](EAIBotState state) -> int
    {
        switch (state)
        {
        case EAIBotState::Evade:
            return 3;
        case EAIBotState::Attack:
            return 2;
        case EAIBotState::Chase:
            return 1;
        case EAIBotState::Patrol:
            return 0;
        case EAIBotState::Disabled:
        default:
            return -1;
        }
    };

    agent.SetDesiredState(DetermineDesiredState(distanceToPlayer, agent.GetTargetVisible(), config));
    if (agent.GetDesiredState() != agent.GetState() &&
        agent.stateHoldRemaining > 0.0f &&
        getStatePriority(agent.GetDesiredState()) <= getStatePriority(agent.GetState()))
    {
        agent.SetDesiredState(agent.GetState());
    }

    const EAIBotState stateBefore = agent.GetState();
    RunBehaviorTree(deltaSeconds, config);
    if (agent.GetState() != stateBefore)
    {
        agent.pathFollower.Clear();
        agent.stateHoldRemaining = config.StateMinHoldTime;
    }

    float speed = config.WalkSpeed;
    if (agent.GetState() == EAIBotState::Chase ||
        agent.GetState() == EAIBotState::Evade ||
        (agent.GetState() == EAIBotState::Attack && distanceToPlayer > config.PreferredCombatRangeMax))
    {
        speed = config.RunSpeed;
    }

    aiCharacter.SetControlIntent(agent.moveDir,
                                 agent.lookDir,
                                 speed,
                                 speed > config.WalkSpeed + 0.05f,
                                 agent.triggerJump);

    aiCharacter.controller.Update(aiCharacter.control ? aiCharacter.control->GetMoveIntent() : agent.moveDir,
                                  aiCharacter.control ? aiCharacter.control->GetDesiredSpeed() : speed,
                                  aiCharacter.ConsumeJumpRequested(),
                                  deltaSeconds);

    glm::vec3 facingDirection = aiCharacter.control ? aiCharacter.control->GetLookIntent() : agent.lookDir;
    if (agent.GetState() != EAIBotState::Attack &&
        glm::length(aiCharacter.controller.GetLinearVelocity()) > 0.2f)
    {
        facingDirection = aiCharacter.controller.GetLinearVelocity();
    }
    agent.yaw = AdvanceYawToward(agent.yaw, facingDirection, config.TurnSpeed, deltaSeconds);

    if (callbacks_.updateAnimationState)
    {
        callbacks_.updateAnimationState(deltaSeconds);
    }
    if (callbacks_.updateNode)
    {
        callbacks_.updateNode();
    }
}

const char* CharacterDemoAIController::GetStateName(EAIBotState state)
{
    return NextGameplay::GetAIAgentStateName(state);
}

NextGameplay::CharacterActor& CharacterDemoAIController::GetAICharacter()
{
    assert(aiCharacter_ != nullptr);
    return *aiCharacter_;
}

const NextGameplay::CharacterActor& CharacterDemoAIController::GetAICharacter() const
{
    assert(aiCharacter_ != nullptr);
    return *aiCharacter_;
}

NextGameplay::AIAgentComponent& CharacterDemoAIController::GetAIAgent()
{
    assert(aiAgent_ != nullptr);
    return *aiAgent_;
}

const NextGameplay::AIAgentComponent& CharacterDemoAIController::GetAIAgent() const
{
    assert(aiAgent_ != nullptr);
    return *aiAgent_;
}

const NextGameplay::CharacterActor& CharacterDemoAIController::GetPlayerCharacter() const
{
    assert(playerCharacter_ != nullptr);
    return *playerCharacter_;
}

NextGameplay::FNavGrid& CharacterDemoAIController::GetNavGrid()
{
    assert(navGrid_ != nullptr);
    return *navGrid_;
}

const NextGameplay::FNavGrid& CharacterDemoAIController::GetNavGrid() const
{
    assert(navGrid_ != nullptr);
    return *navGrid_;
}

std::mt19937& CharacterDemoAIController::GetPatrolRng()
{
    assert(patrolRng_ != nullptr);
    return *patrolRng_;
}

bool CharacterDemoAIController::TryBuildReachablePatrolPath(const glm::vec3& currentPos,
                                                            float referenceHeight,
                                                            size_t startIndex,
                                                            size_t& outPatrolIndex,
                                                            glm::vec3& outTarget,
                                                            std::vector<glm::vec3>& outPath,
                                                            const CharacterDemoAIConfig& config)
{
    auto& agent = GetAIAgent();
    const auto& navGrid = GetNavGrid();

    outPath.clear();
    outTarget = currentPos;

    agent.patrolReachableFound = false;
    agent.patrolUsedNearFallback = false;
    agent.patrolRequestedIndex = startIndex;
    agent.patrolSelectedIndex = startIndex;
    agent.patrolCandidatesTested = 0;
    agent.patrolWaypointCount = 0;
    agent.patrolSelectionMs = 0.0f;
    agent.patrolSelectedDistance = 0.0f;
    agent.patrolSelectedTarget = currentPos;

    if (agent.patrolPoints.empty() || !navGrid.IsBuilt())
    {
        return false;
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    const size_t patrolCount = agent.patrolPoints.size();
    const float minTravelDistance = std::max(config.PatrolMinTravelDistance, config.PatrolPointRadius * 2.0f);
    const std::vector<uint8_t> reachableMask = navGrid.BuildReachabilityMask(currentPos, referenceHeight);
    const int gridWidth = navGrid.GetWidth();
    const int gridHeight = navGrid.GetHeight();

    struct FPatrolCandidateOption
    {
        size_t patrolIndex = 0;
        glm::vec3 resolvedTarget{0.0f};
        std::vector<glm::vec3> path;
        float travelDistance = 0.0f;
        float score = 0.0f;
    };

    std::vector<size_t> candidateOrder(patrolCount);
    for (size_t i = 0; i < patrolCount; ++i)
    {
        candidateOrder[i] = i;
    }
    if (patrolCount > 1)
    {
        std::shuffle(candidateOrder.begin(), candidateOrder.end(), GetPatrolRng());
    }

    const size_t requestedIndex = startIndex % patrolCount;
    const auto requestedIt = std::find(candidateOrder.begin(), candidateOrder.end(), requestedIndex);
    if (requestedIt != candidateOrder.end())
    {
        candidateOrder.erase(requestedIt);
        candidateOrder.insert(candidateOrder.begin(), requestedIndex);
    }

    auto collectCandidates = [&](bool requireMinDistance, bool avoidLastCommitted) -> std::vector<FPatrolCandidateOption>
    {
        std::vector<FPatrolCandidateOption> candidates;
        candidates.reserve(patrolCount);

        for (size_t candidateIndex : candidateOrder)
        {
            const glm::vec3& candidateTarget = agent.patrolPoints[candidateIndex];
            ++agent.patrolCandidatesTested;

            if (avoidLastCommitted && patrolCount > 1 && candidateIndex == agent.patrolLastCommittedIndex)
            {
                continue;
            }

            float bestScore = std::numeric_limits<float>::max();
            float bestTravelDistance = 0.0f;
            glm::vec3 bestReachableTarget(0.0f);
            bool foundReachableTarget = false;

            for (int gz = 0; gz < gridHeight; ++gz)
            {
                for (int gx = 0; gx < gridWidth; ++gx)
                {
                    const size_t cellIndex = static_cast<size_t>(gz * gridWidth + gx);
                    if (cellIndex >= reachableMask.size() || reachableMask[cellIndex] == 0)
                    {
                        continue;
                    }

                    const glm::vec3 cellWorld = navGrid.GetCellWorldPosition(gx, gz);
                    const float travelDistance =
                        glm::length(glm::vec2(cellWorld.x - currentPos.x, cellWorld.z - currentPos.z));
                    if (requireMinDistance && travelDistance < minTravelDistance)
                    {
                        continue;
                    }

                    const float targetDistance =
                        glm::length(glm::vec2(cellWorld.x - candidateTarget.x, cellWorld.z - candidateTarget.z));
                    const float score = targetDistance + travelDistance * 0.05f;
                    if (score < bestScore)
                    {
                        bestScore = score;
                        bestTravelDistance = travelDistance;
                        bestReachableTarget = cellWorld;
                        foundReachableTarget = true;
                    }
                }
            }

            if (!foundReachableTarget)
            {
                continue;
            }

            std::vector<glm::vec3> candidatePath = navGrid.FindPath(currentPos, bestReachableTarget, referenceHeight);
            if (candidatePath.empty())
            {
                continue;
            }

            candidates.push_back(FPatrolCandidateOption{
                .patrolIndex = candidateIndex,
                .resolvedTarget = bestReachableTarget,
                .path = std::move(candidatePath),
                .travelDistance = bestTravelDistance,
                .score = bestScore
            });
        }

        return candidates;
    };

    auto commitCandidate = [&](std::vector<FPatrolCandidateOption>& candidates, bool usedNearFallback) -> bool
    {
        if (candidates.empty())
        {
            return false;
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const FPatrolCandidateOption& lhs, const FPatrolCandidateOption& rhs)
                  {
                      if (lhs.score != rhs.score)
                      {
                          return lhs.score < rhs.score;
                      }
                      return lhs.travelDistance < rhs.travelDistance;
                  });

        const size_t selectionPoolSize = std::min<size_t>(3, candidates.size());
        std::uniform_int_distribution<size_t> distribution(0, selectionPoolSize - 1);
        FPatrolCandidateOption selected = std::move(candidates[distribution(GetPatrolRng())]);

        outPatrolIndex = selected.patrolIndex;
        outTarget = selected.resolvedTarget;
        outPath = std::move(selected.path);
        agent.patrolReachableFound = true;
        agent.patrolUsedNearFallback = usedNearFallback;
        agent.patrolSelectedIndex = selected.patrolIndex;
        agent.patrolWaypointCount = static_cast<int>(outPath.size());
        agent.patrolSelectedDistance = selected.travelDistance;
        agent.patrolSelectedTarget = selected.resolvedTarget;
        agent.patrolLastCommittedIndex = selected.patrolIndex;
        agent.patrolSelectionMs =
            std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - startTime).count();
        return true;
    };

    if (auto candidates = collectCandidates(true, true); commitCandidate(candidates, false))
    {
        return true;
    }

    if (auto candidates = collectCandidates(true, false); commitCandidate(candidates, false))
    {
        return true;
    }

    if (auto candidates = collectCandidates(false, true); commitCandidate(candidates, true))
    {
        return true;
    }

    agent.patrolSelectionMs =
        std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - startTime).count();
    auto candidates = collectCandidates(false, false);
    return commitCandidate(candidates, true);
}

CharacterDemoAIController::EAIBotState CharacterDemoAIController::DetermineDesiredState(float distanceToPlayer,
                                                                                        bool hasCombatTarget,
                                                                                        const CharacterDemoAIConfig& config) const
{
    const auto& agent = GetAIAgent();
    const bool hasChaseTarget = hasCombatTarget || agent.targetMemoryRemaining > 0.0f;
    const bool hasRecentCombatTarget = hasCombatTarget || agent.targetVisibleGraceRemaining > 0.0f ||
                                       agent.targetMemoryRemaining > 0.0f;
    if (!hasChaseTarget)
    {
        return agent.patrolPoints.empty() ? EAIBotState::Disabled : EAIBotState::Patrol;
    }

    const float hysteresis = config.CombatRangeHysteresis;
    const float evadeEnter = config.PreferredCombatRangeMin;
    const float evadeExit = config.PreferredCombatRangeMin + hysteresis;
    const float attackEnterMax = std::min(
        config.FireRange,
        std::max(config.PreferredCombatRangeMin, config.PreferredCombatRangeMax - hysteresis * 0.5f));
    const float attackMinSticky = std::max(0.0f, config.PreferredCombatRangeMin - hysteresis);
    const float attackMaxSticky = std::min(config.FireRange, config.PreferredCombatRangeMax + hysteresis);

    switch (agent.GetPreviousState())
    {
    case EAIBotState::Evade:
        if (hasCombatTarget && distanceToPlayer < evadeExit)
        {
            return EAIBotState::Evade;
        }
        break;
    case EAIBotState::Attack:
        if (hasRecentCombatTarget && distanceToPlayer >= attackMinSticky && distanceToPlayer <= attackMaxSticky)
        {
            return EAIBotState::Attack;
        }
        break;
    case EAIBotState::Chase:
        if (hasChaseTarget && (!hasCombatTarget || distanceToPlayer > attackEnterMax))
        {
            return EAIBotState::Chase;
        }
        break;
    default:
        break;
    }

    if (hasCombatTarget && distanceToPlayer < evadeEnter)
    {
        return EAIBotState::Evade;
    }

    if (hasCombatTarget && distanceToPlayer <= attackEnterMax)
    {
        return EAIBotState::Attack;
    }

    return EAIBotState::Chase;
}

CharacterDemoAIController::EBehaviorTreeStatus CharacterDemoAIController::RunBehaviorTree(float deltaSeconds,
                                                                                           const CharacterDemoAIConfig& config)
{
    auto& agent = GetAIAgent();
    agent.SetBehaviorRootStatus(EBehaviorDebugState::Running);
    agent.SetBehaviorEvadeStatus(EBehaviorDebugState::Inactive);
    agent.SetBehaviorAttackStatus(EBehaviorDebugState::Inactive);
    agent.SetBehaviorChaseStatus(EBehaviorDebugState::Inactive);
    agent.SetBehaviorPatrolStatus(EBehaviorDebugState::Inactive);

    const EBehaviorTreeStatus evadeStatus = RunEvade(deltaSeconds, config);
    agent.SetBehaviorEvadeStatus(ToBehaviorDebugState(evadeStatus));
    if (evadeStatus != EBehaviorTreeStatus::Failure)
    {
        agent.SetBehaviorRootStatus(ToBehaviorDebugState(evadeStatus));
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus attackStatus = RunAttack(deltaSeconds, config);
    agent.SetBehaviorAttackStatus(ToBehaviorDebugState(attackStatus));
    if (attackStatus != EBehaviorTreeStatus::Failure)
    {
        agent.SetBehaviorRootStatus(ToBehaviorDebugState(attackStatus));
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus chaseStatus = RunChase(deltaSeconds, config);
    agent.SetBehaviorChaseStatus(ToBehaviorDebugState(chaseStatus));
    if (chaseStatus != EBehaviorTreeStatus::Failure)
    {
        agent.SetBehaviorRootStatus(ToBehaviorDebugState(chaseStatus));
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus patrolStatus = RunPatrol(deltaSeconds, config);
    agent.SetBehaviorPatrolStatus(ToBehaviorDebugState(patrolStatus));
    agent.SetBehaviorRootStatus(ToBehaviorDebugState(patrolStatus));
    return patrolStatus;
}

CharacterDemoAIController::EBehaviorTreeStatus CharacterDemoAIController::RunEvade(float deltaSeconds,
                                                                                    const CharacterDemoAIConfig& config)
{
    auto& agent = GetAIAgent();
    auto& aiCharacter = GetAICharacter();
    (void)config;

    if (agent.GetDesiredState() != EAIBotState::Evade)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiCharacter.controller.GetPosition();
    const glm::vec3 playerEyePos = callbacks_.getPlayerEyePosition ? callbacks_.getPlayerEyePosition() : glm::vec3(0.0f);
    const glm::vec3 toPlayer = playerEyePos - (callbacks_.getAIBotEyePosition ? callbacks_.getAIBotEyePosition() : aiPos);
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    agent.SetState(EAIBotState::Evade);
    agent.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : agent.lookDir;

    const glm::vec3 strafeDir =
        NormalizeHorizontalOrZero(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toPlayerDir)) * agent.strafeSign;
    const glm::vec3 evadeDir = NormalizeHorizontalOrZero(-toPlayerDir + strafeDir * 0.5f);

    if (GetNavGrid().IsBuilt())
    {
        const glm::vec3 evadeTarget = aiPos + evadeDir * 8.0f;
        if (agent.pathFollower.NeedsRepath(evadeTarget, deltaSeconds, 0.3f, 1.5f))
        {
            auto path = GetNavGrid().FindPath(aiPos, evadeTarget, aiPos.y);
            if (path.empty())
            {
                path = GetNavGrid().FindPath(aiPos, aiPos - toPlayerDir * 8.0f, aiPos.y);
            }
            agent.pathFollower.SetPath(std::move(path), evadeTarget);
        }
        glm::vec3 pathDir = agent.pathFollower.GetMoveDirection(aiPos);
        agent.moveDir = glm::length(pathDir) > 0.001f ? pathDir : evadeDir;
    }
    else
    {
        agent.moveDir = evadeDir;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoAIController::EBehaviorTreeStatus CharacterDemoAIController::RunAttack(float deltaSeconds,
                                                                                     const CharacterDemoAIConfig& config)
{
    (void)deltaSeconds;

    auto& agent = GetAIAgent();
    auto& aiCharacter = GetAICharacter();

    if (agent.GetDesiredState() != EAIBotState::Attack)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiCharacter.controller.GetPosition();
    const glm::vec3 playerEyePos = callbacks_.getPlayerEyePosition ? callbacks_.getPlayerEyePosition() : glm::vec3(0.0f);
    const glm::vec3 aiEyePos = callbacks_.getAIBotEyePosition ? callbacks_.getAIBotEyePosition() : aiPos;
    const glm::vec3 toPlayer = playerEyePos - aiEyePos;
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(playerEyePos.x - aiPos.x, playerEyePos.z - aiPos.z));
    agent.SetState(EAIBotState::Attack);
    agent.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : agent.lookDir;

    glm::vec3 moveDir(0.0f);
    const glm::vec3 botForward(std::sin(agent.yaw), 0.0f, std::cos(agent.yaw));
    const float aimDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool inPreferredRange = distanceToPlayer <= config.PreferredCombatRangeMax;

    if (distanceToPlayer > config.PreferredCombatRangeMax)
    {
        moveDir = toPlayerDir;
    }

    agent.moveDir = NormalizeHorizontalOrZero(moveDir);
    if (agent.fireCooldownRemaining <= 0.0f && aimDot >= config.AimTolerance)
    {
        const glm::vec3 shotDir = glm::normalize(playerEyePos - aiEyePos);
        const glm::vec3 spawnCenter = aiEyePos + shotDir * config.ProjectileSpawnDistance;
        if (callbacks_.spawnProjectile)
        {
            callbacks_.spawnProjectile("EnemyShotBox", spawnCenter, shotDir);
        }
        agent.fireCooldownRemaining = config.FireCooldown;
        if (inPreferredRange)
        {
            agent.strafeSign *= -1.0f;
        }
        agent.patrolPauseRemaining = 0.0f;
        return EBehaviorTreeStatus::Success;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoAIController::EBehaviorTreeStatus CharacterDemoAIController::RunChase(float deltaSeconds,
                                                                                    const CharacterDemoAIConfig& config)
{
    auto& agent = GetAIAgent();
    auto& aiCharacter = GetAICharacter();

    if (agent.GetDesiredState() != EAIBotState::Chase)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiCharacter.controller.GetPosition();
    const glm::vec3 chaseTarget =
        agent.GetTargetVisible() ? GetPlayerCharacter().controller.GetPosition() : agent.lastKnownTargetPosition;
    const glm::vec3 toTarget = chaseTarget - aiPos;
    const glm::vec3 chaseDir = NormalizeHorizontalOrZero(toTarget);
    const float distanceToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
    agent.SetState(EAIBotState::Chase);
    agent.lookDir = glm::length(chaseDir) > 0.001f ? chaseDir : agent.lookDir;

    if (GetNavGrid().IsBuilt())
    {
        if (agent.pathFollower.NeedsRepath(chaseTarget, deltaSeconds, 0.5f, 2.0f))
        {
            auto path = GetNavGrid().FindPath(aiPos, chaseTarget, aiPos.y);
            agent.pathFollower.SetPath(std::move(path), chaseTarget);
        }
        glm::vec3 pathDir = agent.pathFollower.GetMoveDirection(aiPos);
        agent.moveDir = glm::length(pathDir) > 0.001f ? pathDir : chaseDir;
    }
    else
    {
        agent.moveDir = chaseDir;
    }

    if (!agent.GetTargetVisible() && distanceToTarget <= config.PatrolPointRadius)
    {
        agent.targetMemoryRemaining = 0.0f;
        agent.patrolPauseRemaining = config.PatrolPauseTime;
        return EBehaviorTreeStatus::Failure;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoAIController::EBehaviorTreeStatus CharacterDemoAIController::RunPatrol(float deltaSeconds,
                                                                                     const CharacterDemoAIConfig& config)
{
    auto& agent = GetAIAgent();
    auto& aiCharacter = GetAICharacter();

    if (agent.patrolPoints.empty())
    {
        agent.SetState(EAIBotState::Disabled);
        return EBehaviorTreeStatus::Failure;
    }

    agent.SetState(EAIBotState::Patrol);
    if (agent.patrolPauseRemaining > 0.0f)
    {
        return EBehaviorTreeStatus::Running;
    }

    agent.patrolIndex %= agent.patrolPoints.size();
    const glm::vec3 aiPos = aiCharacter.controller.GetPosition();

    if (GetNavGrid().IsBuilt())
    {
        auto assignPatrolPath = [&](size_t reachablePatrolIndex, const glm::vec3& reachableTarget,
                                    std::vector<glm::vec3>&& reachablePath) -> void
        {
            agent.patrolIndex = reachablePatrolIndex;
            agent.pathFollower.SetPath(std::move(reachablePath), reachableTarget);
            agent.patrolProgressAnchor = aiPos;
            agent.patrolStuckTime = 0.0f;
            agent.patrolAbandonedTarget = false;
        };

        auto trySelectNewPatrolTarget = [&](size_t startIndex, bool abandonedCurrentTarget) -> bool
        {
            size_t reachablePatrolIndex = startIndex;
            glm::vec3 reachableTarget(0.0f);
            std::vector<glm::vec3> reachablePath;
            if (!TryBuildReachablePatrolPath(aiPos,
                                             aiPos.y,
                                             startIndex,
                                             reachablePatrolIndex,
                                             reachableTarget,
                                             reachablePath,
                                             config))
            {
                agent.pathFollower.Clear();
                agent.moveDir = glm::vec3(0.0f);
                agent.patrolStuckTime = 0.0f;
                agent.patrolAbandonedTarget = abandonedCurrentTarget;
                agent.patrolPauseRemaining = config.PatrolPauseTime;
                agent.patrolIndex = startIndex % agent.patrolPoints.size();
                return false;
            }

            assignPatrolPath(reachablePatrolIndex, reachableTarget, std::move(reachablePath));
            agent.patrolAbandonedTarget = abandonedCurrentTarget;
            return true;
        };

        if (!agent.pathFollower.waypoints.empty() &&
            agent.pathFollower.IsFinished(aiPos, config.PatrolPointRadius))
        {
            agent.pathFollower.Clear();
            agent.patrolStuckTime = 0.0f;
            agent.patrolAbandonedTarget = false;
            agent.patrolIndex = (agent.patrolIndex + 1) % agent.patrolPoints.size();
            agent.patrolPauseRemaining = config.PatrolPauseTime;
            agent.strafeSign *= -1.0f;
            return EBehaviorTreeStatus::Success;
        }

        const bool hasCommittedPatrolTarget = !agent.pathFollower.waypoints.empty() && agent.patrolReachableFound;
        const glm::vec3 requestedTarget = agent.patrolPoints[agent.patrolIndex];
        const glm::vec3 repathTarget = hasCommittedPatrolTarget ? agent.patrolSelectedTarget : requestedTarget;
        if (agent.pathFollower.NeedsRepath(repathTarget, deltaSeconds, 4.0f, 0.5f))
        {
            if (hasCommittedPatrolTarget)
            {
                std::vector<glm::vec3> refreshedPath = GetNavGrid().FindPath(aiPos, agent.patrolSelectedTarget, aiPos.y);
                if (!refreshedPath.empty())
                {
                    assignPatrolPath(agent.patrolIndex, agent.patrolSelectedTarget, std::move(refreshedPath));
                }
                else if (!trySelectNewPatrolTarget((agent.patrolIndex + 1) % agent.patrolPoints.size(), true))
                {
                    return EBehaviorTreeStatus::Running;
                }
            }
            else if (!trySelectNewPatrolTarget(agent.patrolIndex, false))
            {
                return EBehaviorTreeStatus::Running;
            }
        }

        glm::vec3 pathDir = agent.pathFollower.GetMoveDirection(aiPos);
        if (glm::length(pathDir) > 0.001f)
        {
            agent.moveDir = pathDir;
            agent.lookDir = pathDir;

            const glm::vec3 anchorDelta = aiPos - agent.patrolProgressAnchor;
            const float progressSinceAnchor = glm::length(glm::vec2(anchorDelta.x, anchorDelta.z));
            if (progressSinceAnchor >= config.PatrolProgressResetDistance)
            {
                agent.patrolProgressAnchor = aiPos;
                agent.patrolStuckTime = 0.0f;
                agent.patrolAbandonedTarget = false;
            }
            else
            {
                glm::vec3 horizontalVelocity = aiCharacter.controller.GetLinearVelocity();
                horizontalVelocity.y = 0.0f;
                if (glm::length(horizontalVelocity) <= config.PatrolStuckSpeedThreshold)
                {
                    agent.patrolStuckTime += deltaSeconds;
                }
                else
                {
                    agent.patrolStuckTime = 0.0f;
                }

                if (agent.patrolStuckTime >= config.PatrolStuckTimeout)
                {
                    const size_t nextPatrolIndex = (agent.patrolIndex + 1) % agent.patrolPoints.size();
                    agent.pathFollower.Clear();
                    if (!trySelectNewPatrolTarget(nextPatrolIndex, true))
                    {
                        return EBehaviorTreeStatus::Running;
                    }

                    pathDir = agent.pathFollower.GetMoveDirection(aiPos);
                    if (glm::length(pathDir) > 0.001f)
                    {
                        agent.moveDir = pathDir;
                        agent.lookDir = pathDir;
                    }
                    else
                    {
                        agent.moveDir = glm::vec3(0.0f);
                    }
                }
            }
        }
        else
        {
            agent.pathFollower.Clear();
            agent.moveDir = glm::vec3(0.0f);
            agent.patrolStuckTime = 0.0f;
        }
    }
    else
    {
        const glm::vec3 patrolTarget = agent.patrolPoints[agent.patrolIndex];
        const glm::vec3 toPatrol = patrolTarget - aiPos;
        const glm::vec3 patrolDir = NormalizeHorizontalOrZero(toPatrol);
        const float distanceToPatrol = glm::length(glm::vec2(toPatrol.x, toPatrol.z));
        if (distanceToPatrol <= config.PatrolPointRadius)
        {
            agent.patrolIndex = (agent.patrolIndex + 1) % agent.patrolPoints.size();
            agent.patrolPauseRemaining = config.PatrolPauseTime;
            agent.strafeSign *= -1.0f;
            return EBehaviorTreeStatus::Success;
        }

        agent.moveDir = patrolDir;
        agent.lookDir = glm::length(patrolDir) > 0.001f ? patrolDir : agent.lookDir;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoAIController::EBehaviorDebugState CharacterDemoAIController::ToBehaviorDebugState(EBehaviorTreeStatus status)
{
    switch (status)
    {
    case EBehaviorTreeStatus::Running:
        return EBehaviorDebugState::Running;
    case EBehaviorTreeStatus::Success:
        return EBehaviorDebugState::Success;
    case EBehaviorTreeStatus::Failure:
    default:
        return EBehaviorDebugState::Failure;
    }
}
