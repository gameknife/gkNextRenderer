#pragma once

#include "Common/CoreMinimal.hpp"
#include "NextGameplay/AI/NavGrid.h"
#include "NextGameplay/Character/CharacterActor.h"
#include "NextGameplay/AI/PathFollower.h"
#include "NextGameplay/Components/AIAgentComponent.h"
#include "NextGameplay/Components/CharacterAnimationComponent.h"
#include "NextGameplay/Components/CharacterControlComponent.h"
#include "NextGameplay/Components/CharacterGameplayComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextCharacterController.h"

#include <random>

namespace Runtime { class SkinnedMeshComponent; }

class CharacterDemoGameInstance : public NextGameInstanceBase
{
public:
    CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~CharacterDemoGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OnRenderUI() override;
    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    using EBehaviorTreeStatus = NextGameplay::EBehaviorTreeStatus;
    using EBehaviorDebugState = NextGameplay::EBehaviorDebugState;
    using ECharacterAnimState = NextGameplay::ECharacterAnimState;
    using EAIBotState = NextGameplay::EAIAgentState;
    using ECharacterMovementMode = NextGameplay::ECharacterMovementMode;
    using FNavGridSettings = NextGameplay::FNavGridSettings;
    using FNavGrid = NextGameplay::FNavGrid;
    using FPathFollower = NextGameplay::FPathFollower;

    struct FAIBotRuntime
    {
        NextCharacterController controller;
        std::shared_ptr<Assets::Node> visualNode;
        std::vector<glm::vec3> patrolPoints;
        glm::vec3 moveDir{0.0f};
        glm::vec3 lookDir{0.0f, 0.0f, 1.0f};
        glm::vec3 lastKnownTargetPosition{0.0f};
        float yaw = 0.0f;
        float fireCooldownRemaining = 0.0f;
        float targetMemoryRemaining = 0.0f;
        float patrolPauseRemaining = 0.0f;
        float targetVisibleGraceRemaining = 0.0f;
        float stateHoldRemaining = 0.0f;
        float strafeSign = 1.0f;
        size_t patrolIndex = 0;
        bool targetVisible = false;
        bool triggerJump = false;
        EAIBotState state = EAIBotState::Disabled;
        EAIBotState desiredState = EAIBotState::Disabled;
        EBehaviorDebugState behaviorRootStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorAttackStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorChaseStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        bool patrolReachableFound = false;
        bool patrolUsedNearFallback = false;
        size_t patrolRequestedIndex = 0;
        size_t patrolSelectedIndex = 0;
        int patrolCandidatesTested = 0;
        int patrolWaypointCount = 0;
        float patrolSelectionMs = 0.0f;
        float patrolSelectedDistance = 0.0f;
        float patrolStuckTime = 0.0f;
        bool patrolAbandonedTarget = false;
        size_t patrolLastCommittedIndex = std::numeric_limits<size_t>::max();
        glm::vec3 patrolSelectedTarget{0.0f};
        glm::vec3 patrolProgressAnchor{0.0f};
        FPathFollower pathFollower;
        EAIBotState previousState = EAIBotState::Disabled;
        NextGameplay::CharacterActor character;
        std::shared_ptr<NextGameplay::AIAgentComponent> agentComponent;
    };

    glm::vec3 GetMoveForward() const;
    glm::vec3 GetMoveRight() const;
    glm::vec3 GetViewForward() const;
    glm::vec3 GetEyePosition() const;
    glm::vec3 GetAIBotEyePosition() const;
    float GetCharacterYaw() const;
    void SetFirstPersonMode(bool enabled);
    void FireProjectile();
    void SpawnProjectile(const std::string& nodeName, const glm::vec3& spawnCenter, const glm::vec3& shotDir);
    void UpdateCharacterNode();
    void TryInitCharacterModel();
    void UpdateAnimationState(float deltaSeconds);
    void TryInitAIBotCharacterModel();
    void UpdateAIBotAnimationState(float deltaSeconds);
    const char* GetAnimStateName() const;
    const char* GetAIBotStateName() const;
    void ResetCharacterState();
    void InitAIBot();
    void UpdateAIBot(float deltaSeconds);
    void UpdateAIBotNode();
    void CollectAIBotPatrolPoints();
    bool TryBuildReachablePatrolPath(const glm::vec3& currentPos, float referenceHeight, size_t startIndex,
                                     size_t& outPatrolIndex, glm::vec3& outTarget,
                                     std::vector<glm::vec3>& outPath);
    bool TryGetSceneNodePosition(const std::string& nodeName, glm::vec3& outPosition) const;
    bool HasLineOfSightToPlayer() const;
    EAIBotState DetermineDesiredAIBotState(float distanceToPlayer, bool hasCombatTarget) const;
    EBehaviorTreeStatus RunAIBotBehaviorTree(float deltaSeconds);
    EBehaviorTreeStatus RunAIBotEvade(float deltaSeconds);
    EBehaviorTreeStatus RunAIBotAttack(float deltaSeconds);
    EBehaviorTreeStatus RunAIBotChase(float deltaSeconds);
    EBehaviorTreeStatus RunAIBotPatrol(float deltaSeconds);
    void SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node);
    void PlayCharacterAnimation(const std::string& name, bool loop, float playSpeed = 1.0f);
    void UpdateCharacterFacingYaw(const glm::vec3& moveDir, const glm::vec3& currentVelocity, float deltaSeconds);
    const char* GetMovementModeName() const;
    const char* GetBehaviorDebugStateName(EBehaviorDebugState state) const;
    EBehaviorDebugState ToBehaviorDebugState(EBehaviorTreeStatus status) const;
    void DrawAIDebugMenu();
    void DrawAIBotBehaviorTreeUI() const;
    void DrawNavGridDebugOverlay() const;
    FNavGridSettings CreateNavGridSettings() const;
    void RefreshNavGridFromSceneDirtyRegion();

    NextEngine* engine_;
    NextGameplay::CharacterActor playerCharacter_;

    // Character visual node (box placeholder, hidden once skinned model loads)
    uint32_t capsuleModelId_ = 0;
    uint32_t characterMatId_ = 0;
    uint32_t aiCharacterMatId_ = 0;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMatId_ = 0;
    FAIBotRuntime aiBot_;
    FNavGrid navGrid_;
    std::mt19937 patrolRng_{std::random_device{}()};
    bool sceneHelpersInjected_ = false;
    std::string characterAppendRootName_ = "Mannequin_Medium";

    ECharacterMovementMode movementMode_ = ECharacterMovementMode::CameraAligned;

    // Input state
    bool keyForward_ = false;
    bool keyBack_ = false;
    bool keyLeft_ = false;
    bool keyRight_ = false;
    bool keyJump_ = false;
    bool keySprint_ = false;
    bool mouseCaptured_ = false;
    bool resetMouse_ = true;
    bool showPhysicsDebug_ = false;
    bool footIKEnabled_ = true;
    bool showFootIKDebug_ = false;
    bool showAIDebugMenu_ = false;
    bool showBehaviorTreeDebug_ = false;
    bool showNavGridDebug_ = false;
    bool aiEnabled_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};

    // Camera
    bool firstPersonMode_ = false;
    float yaw_ = 0.0f;       // horizontal rotation in radians
    float pitch_ = 0.0f;     // vertical rotation in radians
    float characterYaw_ = 0.0f;

    // Settings
    float firstPersonEyeHeight_ = 1.55f;
    float aiEyeHeight_ = 1.55f;
    float walkSpeed_ = 4.0f;
    float runSpeed_ = 8.0f;
    float aiWalkSpeed_ = 3.8f;
    float aiRunSpeed_ = 6.2f;
    float mouseSensitivity_ = 0.002f;
    float cameraDistance_ = 5.0f;   // third-person camera distance
    float cameraHeight_ = 2.0f;    // camera height offset above character
    float projectileSize_ = 0.3f;
    float projectileSpawnDistance_ = 0.9f;
    float projectileForce_ = 60000.0f;
    float aiSightRange_ = 28.0f;
    float aiLoseSightRange_ = 34.0f;
    float aiMemoryTime_ = 3.5f;
    float aiTargetVisibleGraceTime_ = 0.75f;
    float aiStateMinHoldTime_ = 0.75f;
    float aiPreferredCombatRangeMin_ = 7.0f;
    float aiPreferredCombatRangeMax_ = 16.0f;
    float aiCombatRangeHysteresis_ = 1.25f;
    float aiFireRange_ = 22.0f;
    float aiFireCooldown_ = 1.25f;
    float aiAimTolerance_ = 0.92f;
    float aiPatrolPointRadius_ = 1.25f;
    float aiPatrolMinTravelDistance_ = 3.0f;
    float aiPatrolPauseTime_ = 0.6f;
    float aiPatrolProgressResetDistance_ = 0.75f;
    float aiPatrolStuckSpeedThreshold_ = 0.2f;
    float aiPatrolStuckTimeout_ = 0.85f;
    float aiTurnSpeed_ = 6.5f;
    float walkStrafePlaySpeed_ = 0.82f;
    float runBackwardPlaySpeed_ = 1.35f;
    float jumpStartHoldTime_ = 0.12f;
    float jumpLandHoldTime_ = 0.18f;
    float characterTurnSpeed_ = 8.0f;
};
