#pragma once

// ============================================================================
// NextAstrobotGameInstance.hpp - Orchestrates the NextAstrobot platformer: loads
// the .scad level, builds the level index, and drives the mechanism / collectible
// / hazard / enemy / interactable systems plus the player, camera and HUD. All
// rules live in the sub-systems; this class wires them together and owns the
// frame order (see docs/projects/nextastrobot/nextastrobot-design.md section 7.4).
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Runtime/GameInstance.hpp"

#include "Application/Game/NextAstrobot/Level/LevelFlow.hpp"
#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/Mechanisms/MechanismSystem.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"
#include "Application/Game/NextAstrobot/Player/FollowCamera.hpp"
#include "Application/Game/NextAstrobot/Player/PlayerController.hpp"
#include "Application/Game/NextAstrobot/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextAstrobot/World/CollectibleSystem.hpp"
#include "Application/Game/NextAstrobot/World/EnemySystem.hpp"
#include "Application/Game/NextAstrobot/World/HazardSystem.hpp"
#include "Application/Game/NextAstrobot/World/InteractableSystem.hpp"

class NextAstrobotGameInstance : public NextGameInstanceBase
{
public:
    NextAstrobotGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextAstrobotGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnRenderUI() override;
    bool ShouldRenderUiDuringScreenshot() const override { return true; }

    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
                        int16_t leftTrigger, int16_t rightTrigger) override;

private:
    void TickWorld(float deltaSeconds);
    void RestartLevel();
    void ApplyRespawn();
    NextAstrobot::FPlayerInput CollectInput() const;
    glm::vec3 SpawnFootPosition() const;
    const NextAstrobot::FLevelDesc& CurrentLevel() const;

    NextAstrobot::FConfig config_{};
    NextAstrobot::FLevelIndex index_{};
    NextAstrobot::FLevelFlow flow_{};
    NextAstrobot::FPlayerController player_;
    NextAstrobot::FPlayerRigVisual rig_;
    NextAstrobot::FFollowCamera camera_;
    NextAstrobot::FLevelCameras levelCameras_;
    NextAstrobot::FMechanismSystem mechanisms_;
    NextAstrobot::FCollectibleSystem collectibles_;
    NextAstrobot::FHazardSystem hazards_;
    NextAstrobot::FEnemySystem enemies_;
    NextAstrobot::FInteractableSystem interactables_;

    std::vector<std::string> indexWarnings_;
    size_t levelCursor_ = 0;
    bool sceneReady_ = false;
    float levelTime_ = 0.0f;
    // Last frame's foot position: the mechanisms run before the character does, so the
    // seesaw and the surface tests have to read where the player actually stood.
    glm::vec3 previousFoot_{0.0f};
    float spawnYaw_ = 0.0f;

    // input state
    bool keyMove_[4] = {false, false, false, false};
    bool jumpHeld_ = false;
    bool jumpPressed_ = false;
    bool punchPressed_ = false;
    bool anyKeyPressed_ = false;
    glm::vec2 stickMove_{0.0f};
    float stickCameraYaw_ = 0.0f;
    bool mouseLookActive_ = false;
    glm::dvec2 mousePosition_{0.0, 0.0};
    bool mousePositionValid_ = false;

    // presentation
    std::string toast_;
    float toastTimer_ = 0.0f;
    bool showDebugPanel_ = false;

    // debug cvars
    std::string teleportRequest_;
    std::string rideRequest_;
    bool godMode_ = false;
    float timeScale_ = 1.0f;
    std::string forcedState_;
};
