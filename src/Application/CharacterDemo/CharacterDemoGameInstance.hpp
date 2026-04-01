#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextCharacterController.h"

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
    glm::vec3 GetMoveForward() const;
    glm::vec3 GetMoveRight() const;
    glm::vec3 GetViewForward() const;
    glm::vec3 GetEyePosition() const;
    float GetCharacterYaw() const;
    void SetFirstPersonMode(bool enabled);
    void FireProjectile();
    void UpdateCharacterNode();
    void TryInitCharacterModel();
    void UpdateAnimationState(float deltaSeconds);
    void MapAnimationNames(const std::vector<std::string>& names);
    const char* GetAnimStateName() const;
    void ResetCharacterState();
    void SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node);
    void PlayCharacterAnimation(const std::string& name, bool loop, float playSpeed = 1.0f);
    void UpdateCharacterFacingYaw(const glm::vec3& moveDir, const glm::vec3& currentVelocity, float deltaSeconds);
    void UpdateCharacterAnimationPostProcess();
    const char* GetMovementModeName() const;

    NextEngine* engine_;
    NextCharacterController characterController_;

    // Character visual node (box placeholder, hidden once skinned model loads)
    std::shared_ptr<Assets::Node> characterNode_;
    uint32_t capsuleModelId_ = 0;
    uint32_t characterMatId_ = 0;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMatId_ = 0;

    // Skinned character model
    std::shared_ptr<Assets::Node> skinnedCharacterRoot_;
    Runtime::SkinnedMeshComponent* primarySkinnedMeshComp_ = nullptr;
    std::vector<Runtime::SkinnedMeshComponent*> skinnedMeshComps_;
    bool characterModelLoaded_ = false;
    bool characterLoadRequested_ = false;
    bool sceneHelpersInjected_ = false;
    std::string characterAppendRootName_ = "Mannequin_Medium";

    // Animation state machine
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
    bool showPhysicsDebug_ = false;
    bool footIKEnabled_ = true;
    bool showFootIKDebug_ = false;
    glm::dvec2 mousePos_{0.0, 0.0};

    // Camera
    bool firstPersonMode_ = false;
    float yaw_ = 0.0f;       // horizontal rotation in radians
    float pitch_ = 0.0f;     // vertical rotation in radians
    float characterYaw_ = 0.0f;

    // Settings
    float firstPersonEyeHeight_ = 1.55f;
    float walkSpeed_ = 4.0f;
    float runSpeed_ = 8.0f;
    float mouseSensitivity_ = 0.002f;
    float cameraDistance_ = 5.0f;   // third-person camera distance
    float cameraHeight_ = 2.0f;    // camera height offset above character
    float projectileSize_ = 0.3f;
    float projectileSpawnDistance_ = 0.9f;
    float projectileForce_ = 60000.0f;
    float walkStrafePlaySpeed_ = 0.82f;
    float runBackwardPlaySpeed_ = 1.35f;
    float jumpStartHoldTime_ = 0.12f;
    float jumpLandHoldTime_ = 0.18f;
    float characterTurnSpeed_ = 8.0f;
};
