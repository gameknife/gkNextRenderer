#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "CharacterDemoAIController.hpp"
#include "CharacterDemoConfig.hpp"
#include "CharacterDemoAIDebugUI.hpp"
#include "Engine/NextGameplay/AI/NavGrid.h"
#include "Engine/NextGameplay/Character/CharacterActor.h"
#include "Engine/NextGameplay/Components/AIAgentComponent.h"
#include "Engine/NextGameplay/Components/CharacterAnimationComponent.h"
#include "Engine/NextGameplay/Components/CharacterControlComponent.h"
#include "Engine/NextGameplay/Components/CharacterGameplayComponent.h"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Subsystems/NextCharacterController.h"

#include <random>

namespace Runtime { class SkinnedMeshComponent; }

class CharacterDemoGameInstance : public NextGameInstanceBase
{
public:
    CharacterDemoGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
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
    void DrawAdditionalPhysicsDebugOverlay(const Assets::Camera& camera) const override;

    bool SupportsAppDebugShortcut(SDL_Keycode key) const override;
    bool IsAppDebugShortcutActive(SDL_Keycode key) const override;
    bool SetAppDebugShortcutActive(SDL_Keycode key, bool active) override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    using EBehaviorDebugState = NextGameplay::EBehaviorDebugState;
    using ECharacterAnimState = NextGameplay::ECharacterAnimState;
    using EAIBotState = NextGameplay::EAIAgentState;
    using ECharacterMovementMode = NextGameplay::ECharacterMovementMode;
    using FNavGridSettings = NextGameplay::FNavGridSettings;
    using FNavGrid = NextGameplay::FNavGrid;
    struct FAIBotRuntime
    {
        std::shared_ptr<Assets::Node> visualNode;
        NextGameplay::CharacterActor character;
        std::shared_ptr<NextGameplay::AIAgentComponent> agentComponent;
    };

    NextGameplay::AIAgentComponent& GetAIBotAgent();
    const NextGameplay::AIAgentComponent& GetAIBotAgent() const;

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
    bool TryGetSceneNodePosition(const std::string& nodeName, glm::vec3& outPosition) const;
    bool HasLineOfSightToPlayer() const;
    void PlayCharacterAnimation(const std::string& name, bool loop, float playSpeed = 1.0f);
    void UpdateCharacterFacingYaw(const glm::vec3& moveDir, const glm::vec3& currentVelocity, float deltaSeconds);
    CharacterDemoAIDebugUI::FContext CreateAIDebugUIContext() const;
    void DrawAIDebugMenu();
    void DrawAIBotBehaviorTreeUI() const;
    void DrawNavGridDebugOverlay() const;
    FNavGridSettings CreateNavGridSettings() const;
    const CharacterDemoConfig& GetConfig() const { return config_; }
    CharacterDemoAIConfig CreateAISettings() const;
    void RefreshNavGridFromSceneDirtyRegion();

    NextGameplay::CharacterActor playerCharacter_;

    // Character visual node (box placeholder, hidden once skinned model loads)
    uint32_t capsuleModelId_ = 0;
    uint32_t characterMatId_ = 0;
    uint32_t aiCharacterMatId_ = 0;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMatId_ = 0;
    FAIBotRuntime aiBot_;
    CharacterDemoAIController aiController_;
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
    bool footIKEnabled_ = true;
    bool showFootIKDebug_ = false;
    bool showAIDebugMenu_ = false;
    bool showBehaviorTreeDebug_ = false;
    bool showNavGridDebug_ = false;
    glm::dvec2 mousePos_{0.0, 0.0};

    // Camera
    bool firstPersonMode_ = false;
    float yaw_ = 0.0f;       // horizontal rotation in radians
    float pitch_ = 0.0f;     // vertical rotation in radians
    float characterYaw_ = 0.0f;

    // Settings
    CharacterDemoConfig config_{};
};
