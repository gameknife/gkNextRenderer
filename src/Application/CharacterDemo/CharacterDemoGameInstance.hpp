#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextCharacterController.h"
#include "Runtime/AI/NavGrid.h"
#include "Runtime/AI/PathFollower.h"

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
    enum class EBehaviorTreeStatus
    {
        Failure,
        Success,
        Running,
    };

    enum class EBehaviorDebugState
    {
        Inactive,
        Failure,
        Success,
        Running,
    };

    enum class ECharacterAnimState
    {
        Idle,
        WalkForward,
        WalkBackward,
        WalkStrafeLeft,
        WalkStrafeRight,
        RunForward,
        RunBackward,
        RunStrafeLeft,
        RunStrafeRight,
        JumpStart,
        JumpLoop,
        JumpLand,
    };

    enum class EAIBotState
    {
        Disabled,
        Patrol,
        Chase,
        Evade,
        Attack,
    };

    struct FAIBot
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
        float strafeSign = 1.0f;
        size_t patrolIndex = 0;
        bool targetVisible = false;
        bool triggerJump = false;
        EAIBotState state = EAIBotState::Disabled;
        std::shared_ptr<Assets::Node> skinnedRoot;
        Runtime::SkinnedMeshComponent* primarySkinnedMeshComp = nullptr;
        std::vector<Runtime::SkinnedMeshComponent*> skinnedMeshComps;
        std::string appendRootName = "Mannequin_Medium_1";
        ECharacterAnimState animState = ECharacterAnimState::Idle;
        bool characterModelLoaded = false;
        bool characterLoadRequested = false;
        bool wasOnGroundLastFrame = true;
        float jumpStartHoldTimeRemaining = 0.0f;
        float jumpLandHoldTimeRemaining = 0.0f;
        EBehaviorDebugState behaviorRootStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorAttackStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorChaseStatus = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        FPathFollower pathFollower;
        EAIBotState previousState = EAIBotState::Disabled;
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
    void MapAnimationNames(const std::vector<std::string>& names);
    const char* GetAnimStateName() const;
    const char* GetAIBotStateName() const;
    void ResetCharacterState();
    void InitAIBot();
    void UpdateAIBot(float deltaSeconds);
    void UpdateAIBotNode();
    void CollectAIBotPatrolPoints();
    bool TryGetSceneNodePosition(const std::string& nodeName, glm::vec3& outPosition) const;
    bool HasLineOfSightToPlayer() const;
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
    void UpdateCharacterAnimationPostProcess();
    const char* GetMovementModeName() const;
    const char* GetBehaviorDebugStateName(EBehaviorDebugState state) const;
    EBehaviorDebugState ToBehaviorDebugState(EBehaviorTreeStatus status) const;
    void DrawAIBotBehaviorTreeUI() const;
    void DrawNavGridDebugOverlay() const;

    NextEngine* engine_;
    NextCharacterController characterController_;

    // Character visual node (box placeholder, hidden once skinned model loads)
    std::shared_ptr<Assets::Node> characterNode_;
    uint32_t capsuleModelId_ = 0;
    uint32_t characterMatId_ = 0;
    uint32_t aiCharacterMatId_ = 0;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMatId_ = 0;
    FAIBot aiBot_;
    FNavGrid navGrid_;

    // Skinned character model
    std::shared_ptr<Assets::Node> skinnedCharacterRoot_;
    Runtime::SkinnedMeshComponent* primarySkinnedMeshComp_ = nullptr;
    std::vector<Runtime::SkinnedMeshComponent*> skinnedMeshComps_;
    bool characterModelLoaded_ = false;
    bool characterLoadRequested_ = false;
    bool sceneHelpersInjected_ = false;
    std::string characterAppendRootName_ = "Mannequin_Medium";

    ECharacterAnimState currentAnimState_ = ECharacterAnimState::Idle;
    std::string animIdle_;
    std::string animWalkForward_;
    std::string animWalkBackward_;
    std::string animStrafeLeft_;
    std::string animStrafeRight_;
    std::string animRunForward_;
    std::string animRunBackward_;
    std::string animRunStrafeLeft_;
    std::string animRunStrafeRight_;
    std::string animJumpStart_;
    std::string animJumpLoop_;
    std::string animJumpLand_;
    bool wasOnGroundLastFrame_ = true;
    float jumpStartHoldTimeRemaining_ = 0.0f;
    float jumpLandHoldTimeRemaining_ = 0.0f;

    enum class ECharacterMovementMode
    {
        CameraAligned,
        MoveAligned,
    };
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
    bool showGraphicsDebug_ = false;
    bool showPhysicsDebug_ = false;
    bool footIKEnabled_ = true;
    bool showFootIKDebug_ = false;
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
    float aiPreferredCombatRangeMin_ = 7.0f;
    float aiPreferredCombatRangeMax_ = 16.0f;
    float aiCombatRangeHysteresis_ = 1.25f;
    float aiFireRange_ = 22.0f;
    float aiFireCooldown_ = 1.25f;
    float aiAimTolerance_ = 0.92f;
    float aiPatrolPointRadius_ = 1.25f;
    float aiPatrolPauseTime_ = 0.6f;
    float aiTurnSpeed_ = 6.5f;
    float walkStrafePlaySpeed_ = 0.82f;
    float runBackwardPlaySpeed_ = 1.35f;
    float jumpStartHoldTime_ = 0.12f;
    float jumpLandHoldTime_ = 0.18f;
    float characterTurnSpeed_ = 8.0f;
};
