#pragma once

#include "Common/CoreMinimal.hpp"
#include "CharacterDemoConfig.hpp"
#include "NextGameplay/AI/NavGrid.h"
#include "NextGameplay/Character/CharacterActor.h"
#include "NextGameplay/Components/AIAgentComponent.h"

class CharacterDemoAIController
{
public:
    using EAIBotState = NextGameplay::EAIAgentState;
    using EBehaviorTreeStatus = NextGameplay::EBehaviorTreeStatus;
    using EBehaviorDebugState = NextGameplay::EBehaviorDebugState;

    struct FCallbacks
    {
        std::function<glm::vec3()> getPlayerEyePosition;
        std::function<glm::vec3()> getAIBotEyePosition;
        std::function<bool(const std::string&, glm::vec3&)> tryGetSceneNodePosition;
        std::function<bool()> hasLineOfSightToPlayer;
        std::function<void(const std::string&, const glm::vec3&, const glm::vec3&)> spawnProjectile;
        std::function<void(float)> updateAnimationState;
        std::function<void()> updateNode;
    };

    void Bind(NextGameplay::CharacterActor* aiCharacter,
              NextGameplay::AIAgentComponent* aiAgent,
              const NextGameplay::CharacterActor* playerCharacter,
              NextGameplay::FNavGrid* navGrid,
              std::mt19937* patrolRng,
              FCallbacks callbacks);
    void Reset();
    void OnNavGridRebuilt();
    void CollectPatrolPoints();
    void Update(float deltaSeconds, const CharacterDemoAIConfig& config);

    static const char* GetStateName(EAIBotState state);

private:
    NextGameplay::CharacterActor& GetAICharacter();
    const NextGameplay::CharacterActor& GetAICharacter() const;
    NextGameplay::AIAgentComponent& GetAIAgent();
    const NextGameplay::AIAgentComponent& GetAIAgent() const;
    const NextGameplay::CharacterActor& GetPlayerCharacter() const;
    NextGameplay::FNavGrid& GetNavGrid();
    const NextGameplay::FNavGrid& GetNavGrid() const;
    std::mt19937& GetPatrolRng();

    bool TryBuildReachablePatrolPath(const glm::vec3& currentPos,
                                     float referenceHeight,
                                     size_t startIndex,
                                     size_t& outPatrolIndex,
                                     glm::vec3& outTarget,
                                     std::vector<glm::vec3>& outPath,
                                     const CharacterDemoAIConfig& config);
    EAIBotState DetermineDesiredState(float distanceToPlayer, bool hasCombatTarget, const CharacterDemoAIConfig& config) const;
    EBehaviorTreeStatus RunBehaviorTree(float deltaSeconds, const CharacterDemoAIConfig& config);
    EBehaviorTreeStatus RunEvade(float deltaSeconds, const CharacterDemoAIConfig& config);
    EBehaviorTreeStatus RunAttack(float deltaSeconds, const CharacterDemoAIConfig& config);
    EBehaviorTreeStatus RunChase(float deltaSeconds, const CharacterDemoAIConfig& config);
    EBehaviorTreeStatus RunPatrol(float deltaSeconds, const CharacterDemoAIConfig& config);

    static EBehaviorDebugState ToBehaviorDebugState(EBehaviorTreeStatus status);

    NextGameplay::CharacterActor* aiCharacter_ = nullptr;
    NextGameplay::AIAgentComponent* aiAgent_ = nullptr;
    const NextGameplay::CharacterActor* playerCharacter_ = nullptr;
    NextGameplay::FNavGrid* navGrid_ = nullptr;
    std::mt19937* patrolRng_ = nullptr;
    FCallbacks callbacks_;
};
