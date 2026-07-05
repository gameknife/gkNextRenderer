#include "CharacterDemoGameInstance.hpp"
#include "CharacterDemoAIDebugUI.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <cassert>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Assets/Loaders/FSceneLoader.h"
#include "Gameplay/Gameplay/GameplayMath.hpp"
#include "Gameplay/Character/CharacterControllerDebugDraw.hpp"
#include "Gameplay/Reflection/GameplayReflectionRegistry.h"
#include "Gameplay/Utilities/SceneNodeUtils.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"
#include "Engine/Runtime/Scene/NodeUtils.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/DevTools/PhysicsDebugOverlay.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "CharacterPlaygroundScene.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    RegisterCharacterPlaygroundScene();
    return std::make_unique<CharacterDemoGameInstance>(config, options, engine);
}

using NextGameplay::NormalizeHorizontalOrZero;
using NextGameplay::SetNodeVisibilityRecursive;

CharacterDemoGameInstance::CharacterDemoGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    // config.Height = 720;
    // config.Width = 1280;
}

NextGameplay::AIAgentComponent& CharacterDemoGameInstance::GetAIBotAgent()
{
    assert(aiBot_.agentComponent != nullptr);
    return *aiBot_.agentComponent;
}

const NextGameplay::AIAgentComponent& CharacterDemoGameInstance::GetAIBotAgent() const
{
    assert(aiBot_.agentComponent != nullptr);
    return *aiBot_.agentComponent;
}

void CharacterDemoGameInstance::OnInit()
{
    NextGameplay::RegisterGameplayReflection();

    // CharacterDemo depends on an optional asset (skinned character mesh).
    // Bail out with a helpful prompt instead of crashing later in SkinnedMesh setup.
    constexpr const char* kCharacterProbe = "assets/models/characters/Mannequin_Medium.glb";
    if (!Utilities::FileHelper::IsAssetAvailable(kCharacterProbe))
    {
        const char* message =
            "CharacterDemo needs the optional asset pack (character mesh).\n\n"
            "Run one of the following from the repo root, then relaunch:\n"
            "  scripts/fetch-paks.sh --optional      (Linux / macOS / Git Bash)\n"
            "  scripts\\fetch-paks.bat --optional    (Windows)";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CharacterDemo - Missing Optional Assets",
                                 message, GetEngine().GetWindow().Handle());
        SPDLOG_ERROR("CharacterDemo: optional asset pack missing; aborting OnInit");
        GetEngine().RequestClose();
        return;
    }

    // CharacterDemo appends the skinned character after the base playground is already loaded.
    // The append path triggers a full scene mesh-buffer rebuild, so the original scene meshes must
    // keep their CPU copy alive instead of being discarded after the first upload.
    GOption->KeepCPUMeshData = true;

    // Default to procedural playground; user can override via command line --scene
    std::string initialScene = "CharacterPlayground.proc";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }
    GetEngine().RequestLoadScene({.filename = initialScene});
}

void CharacterDemoGameInstance::OnTick(double deltaSeconds)
{
    if (!playerCharacter_.controller.IsValid())
    {
        return;
    }

    // Try to find and init the skinned character model once it's loaded
    if (playerCharacter_.modelLoadRequested && !playerCharacter_.modelLoaded)
    {
        TryInitCharacterModel();
    }
    if (aiBot_.character.modelLoadRequested && !aiBot_.character.modelLoaded)
    {
        TryInitAIBotCharacterModel();
    }

    RefreshNavGridFromSceneDirtyRegion();

    // Build movement direction in world space from camera yaw
    glm::vec3 forward = GetMoveForward();
    glm::vec3 right = GetMoveRight();

    glm::vec3 moveDir(0.0f);
    if (keyForward_) moveDir += forward;
    if (keyBack_)    moveDir -= forward;
    if (keyRight_)   moveDir += right;
    if (keyLeft_)    moveDir -= right;

    if (glm::length(moveDir) > 0.001f)
    {
        moveDir = glm::normalize(moveDir);
    }

    float speed = keySprint_ ? config_.Player.RunSpeed : config_.Player.WalkSpeed;
    playerCharacter_.SetControlIntent(moveDir, GetViewForward(), speed, keySprint_, keyJump_);

    playerCharacter_.controller.Update(playerCharacter_.control ? playerCharacter_.control->GetMoveIntent() : moveDir,
                                       playerCharacter_.control ? playerCharacter_.control->GetDesiredSpeed() : speed,
                                       playerCharacter_.ConsumeJumpRequested(), static_cast<float>(deltaSeconds));
    keyJump_ = false; // consume jump

    const glm::vec3 currentVelocity = playerCharacter_.controller.GetLinearVelocity();

    UpdateCharacterFacingYaw(playerCharacter_.control ? playerCharacter_.control->GetMoveIntent() : moveDir,
                             currentVelocity, static_cast<float>(deltaSeconds));
    UpdateCharacterNode();
    UpdateAnimationState(static_cast<float>(deltaSeconds));
    UpdateAIBot(static_cast<float>(deltaSeconds));
}

void CharacterDemoGameInstance::OnDestroy()
{
    playerCharacter_.controller.Destroy();
    aiBot_.character.controller.Destroy();
}

void CharacterDemoGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    //cvars.SetDefaultFromString("r.superResolution", "4", &error);
    cvars.SetDefaultFromString("r.fastGather", "true", &error);
    //cvars.SetDefaultFromString("r.dlss", "true", &error);
}

void CharacterDemoGameInstance::BeforeSceneRebuild(
    std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models,
    std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights,
    std::vector<Assets::AnimationTrack>& tracks)
{
    if (sceneHelpersInjected_)
    {
        return;
    }

    // Create a capsule-like visual: a box that approximates the character shape
    // Height 1.75, width 0.6
    const float halfW = 0.3f;
    const float halfH = 0.875f;
    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-halfW, 0.0f, -halfW),
        glm::vec3(halfW, halfH * 2.0f, halfW)));
    capsuleModelId_ = static_cast<uint32_t>(models.size() - 1);

    // A distinct green material for the character
    characterMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.2f, 0.8f, 0.3f));

    aiCharacterMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.9f, 0.25f, 0.2f));

    const float halfProjectile = config_.Projectile.Size * 0.5f;
    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-halfProjectile),
        glm::vec3(halfProjectile)));
    projectileModelId_ = static_cast<uint32_t>(models.size() - 1);

    materials.push_back({Assets::Material::Mixture(glm::vec3(0.95f, 0.95f, 0.98f), 0.1f)});
    projectileMatId_ = static_cast<uint32_t>(materials.size() - 1);

    sceneHelpersInjected_ = true;
}

void CharacterDemoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // Create the character controller
    FCharacterControllerSettings settings;
    settings.height = 2.0f;
    settings.radius = 0.5f;
    settings.maxStrength = 4000.0f;
    settings.mass = 200.f;

    // Place character at scene camera position or a default spawn
    const auto& cam = GetEngine().GetScene().GetRenderCamera();
    glm::mat4 invModelView = glm::inverse(cam.ModelView);
    glm::vec3 camPos = glm::vec3(invModelView[3]);
    settings.initialPosition = glm::vec3(camPos.x, camPos.y, camPos.z);

    playerCharacter_.CreateController(GetEngine().GetPhysicsEngine(), settings);

    // Extract yaw from camera
    glm::vec3 camForward = -glm::vec3(invModelView[2]);
    yaw_ = std::atan2(camForward.x, camForward.z);
    pitch_ = std::asin(glm::clamp(camForward.y, -1.0f, 1.0f));
    characterYaw_ = yaw_;

    NextGameplay::FCharacterActorSetup playerSetup;
    playerSetup.actorName = "CharacterActor";
    playerSetup.initialPosition = settings.initialPosition;
    playerSetup.appendRootName = characterAppendRootName_;
    playerSetup.controlSource = NextGameplay::ECharacterControlSource::Player;
    playerSetup.movementMode = movementMode_;
    playerSetup.firstPersonMode = firstPersonMode_;
    playerSetup.eyeHeight = config_.Player.EyeHeight;
    playerSetup.facingYaw = characterYaw_;
    playerSetup.walkStrafePlaySpeed = config_.Animation.WalkStrafePlaySpeed;
    playerSetup.runBackwardPlaySpeed = config_.Animation.RunBackwardPlaySpeed;
    playerSetup.jumpStartHoldTime = config_.Animation.JumpStartHoldTime;
    playerSetup.jumpLandHoldTime = config_.Animation.JumpLandHoldTime;
    playerCharacter_.Initialize(GetEngine().GetScene(), playerSetup);
    playerCharacter_.SetControlIntent(glm::vec3(0.0f), GetViewForward(), 0.0f, false, false);
    playerCharacter_.CreatePlaceholderVisual(GetEngine().GetScene(), "CharacterBody", capsuleModelId_, characterMatId_);
    InitAIBot();

    // Build navigation grid from scene BVH for AI pathfinding
    {
        const FNavGridSettings navSettings = CreateNavGridSettings();
        navGrid_.Build(GetEngine().GetScene().GetCPUAccelerationStructure(), navSettings);
        GetEngine().GetScene().GetCPUAccelerationStructure().ClearNavRelevantDirtyBounds();
    }

    SetFirstPersonMode(firstPersonMode_);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetScene().GetEnvSettings().SkyIdx = 2;
    
    // Load the skinned character model asynchronously
    GetEngine().RequestLoadScene({.filename = "assets/models/characters/Mannequin_Medium.glb", .append = true});
    playerCharacter_.SetModelLoadRequested(true);
    GetEngine().RequestLoadScene({.filename = "assets/models/characters/Mannequin_Medium.glb", .append = true});
    aiBot_.character.SetModelLoadRequested(true);

    // Capture mouse
    mouseCaptured_ = true;
    resetMouse_ = true;
    SDL_SetWindowRelativeMouseMode(GetEngine().GetWindow().Handle(), true);
}

void CharacterDemoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();
    ResetCharacterState();
    sceneHelpersInjected_ = false;
}

CharacterDemoGameInstance::FNavGridSettings CharacterDemoGameInstance::CreateNavGridSettings() const
{
    const glm::vec3 sceneMin = GetEngine().GetScene().GetSceneAABBMin();
    const glm::vec3 sceneMax = GetEngine().GetScene().GetSceneAABBMax();

    FNavGridSettings navSettings;
    navSettings.cellSize = 0.75f;
    navSettings.agentRadius = playerCharacter_.controller.GetRadius() + 0.02f;
    navSettings.maxSlopeAngle = 50.0f;
    navSettings.clearanceHeight = std::max(playerCharacter_.controller.GetHeight(), 0.1f);
    navSettings.maxStepHeight = 0.35f;
    navSettings.worldMin = glm::vec3(sceneMin.x - 2.0f, 0.0f, sceneMin.z - 2.0f);
    navSettings.worldMax = glm::vec3(sceneMax.x + 2.0f, 0.0f, sceneMax.z + 2.0f);
    navSettings.sampleCeiling = sceneMax.y + 5.0f;
    navSettings.floorHeightTolerance = 1.0f;
    return navSettings;
}

CharacterDemoAIConfig CharacterDemoGameInstance::CreateAISettings() const
{
    CharacterDemoAIConfig settings = config_.AI;
    settings.ProjectileSpawnDistance = config_.Projectile.SpawnDistance;
    return settings;
}

void CharacterDemoGameInstance::RefreshNavGridFromSceneDirtyRegion()
{
    glm::vec3 dirtyWorldMin(0.0f);
    glm::vec3 dirtyWorldMax(0.0f);
    auto& cpuAS = GetEngine().GetScene().GetCPUAccelerationStructure();
    if (!cpuAS.ConsumeNavRelevantDirtyBounds(dirtyWorldMin, dirtyWorldMax))
    {
        return;
    }

    if (!navGrid_.IsBuilt())
    {
        navGrid_.Build(cpuAS, CreateNavGridSettings());
    }
    else
    {
        navGrid_.RebuildDirtyRegion(cpuAS, dirtyWorldMin, dirtyWorldMax);
    }

    aiController_.OnNavGridRebuilt();
}

bool CharacterDemoGameInstance::OnRenderUI()
{
    if (!playerCharacter_.controller.IsValid())
    {
        return false;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(280, 0));
    ImGui::Begin("Character Demo", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    glm::vec3 pos = playerCharacter_.controller.GetPosition();
    glm::vec3 vel = playerCharacter_.controller.GetLinearVelocity();
    bool onGround = playerCharacter_.controller.IsOnGround();

    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("Velocity: %.1f, %.1f, %.1f", vel.x, vel.y, vel.z);
    ImGui::Text("On Ground: %s", onGround ? "Yes" : "No");
    ImGui::Text("View: %s", firstPersonMode_ ? "FPS" : "TPS");
    ImGui::Text("Move Mode: %s", NextGameplay::GetCharacterMovementModeName(movementMode_));
    ImGui::Text("Graphics Debug: %s", GetEngine().GetShowFlags().DebugGraphicsPanel ? "On" : "Off");
    ImGui::Text("Physics Debug: %s", GetEngine().GetShowFlags().DebugPhysicsOverlay ? "On" : "Off");
    ImGui::Text("AI Debug Menu: %s", showAIDebugMenu_ ? "On" : "Off");
    ImGui::Text("AI BT Overlay: %s", showBehaviorTreeDebug_ ? "On" : "Off");
    ImGui::Text("NavGrid Overlay: %s", showNavGridDebug_ ? "On" : "Off");
    ImGui::Text("AI: %s", config_.AI.Enabled ? GetAIBotStateName() : "Disabled");
    if (playerCharacter_.modelLoaded)
    {
        ImGui::Text("Anim State: %s", GetAnimStateName());
        if (playerCharacter_.primarySkinnedMeshComp)
        {
            ImGui::Text("Playing: %s", playerCharacter_.primarySkinnedMeshComp->GetCurrentAnimationName().c_str());
        }
    }
    else if (playerCharacter_.modelLoadRequested)
    {
        ImGui::Text("Character: Loading...");
    }
    if (aiBot_.character.controller.IsValid())
    {
        const auto* agent = aiBot_.agentComponent.get();
        const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
        const float botDistance = glm::distance(aiPos, pos);
        ImGui::Text("AI Pos: %.1f, %.1f, %.1f", aiPos.x, aiPos.y, aiPos.z);
        ImGui::Text("AI Dist: %.1f | Visible: %s | LOS: %s", botDistance,
                    (agent && agent->GetTargetVisible()) ? "Yes" : "No",
                    HasLineOfSightToPlayer() ? "Yes" : "No");
        if (aiBot_.character.modelLoaded && aiBot_.character.primarySkinnedMeshComp)
        {
            ImGui::Text("AI Clip: %s", aiBot_.character.primarySkinnedMeshComp->GetCurrentAnimationName().c_str());
        }
    }
    ImGui::Separator();
    ImGui::Text("WASD - Move | Shift - Run");
    ImGui::Text("Space - Jump | Mouse - Look");
    ImGui::Text("V - Toggle FPS/TPS | Tab - Move Mode");
    ImGui::Text("LMB - Shoot | F1 - Physics | F2 - Graphics | Q - Next Renderer");
    ImGui::Text("1-8 - View Modes | F8 - AI Debug Menu | F9 - IK Debug");
    ImGui::Text("ESC - Release Mouse");

    ImGui::SliderFloat("Walk Speed", &config_.Player.WalkSpeed, 1.0f, 10.0f);
    ImGui::SliderFloat("Run Speed", &config_.Player.RunSpeed, 5.0f, 20.0f);
    ImGui::SliderFloat("Camera Dist", &config_.Camera.Distance, 1.0f, 15.0f);
    if (ImGui::Checkbox("Enable AI", &config_.AI.Enabled))
    {
        if (aiBot_.agentComponent)
        {
            aiBot_.agentComponent->SetEnabled(config_.AI.Enabled);
        }
    }
    ImGui::SliderFloat("AI Sight", &config_.AI.SightRange, 8.0f, 60.0f);
    ImGui::SliderFloat("AI Fire Range", &config_.AI.FireRange, 4.0f, 40.0f);
    ImGui::SliderFloat("AI Fire Cooldown", &config_.AI.FireCooldown, 0.2f, 4.0f);

    ImGui::End();

    if (showAIDebugMenu_)
    {
        DrawAIDebugMenu();
    }

    if (showBehaviorTreeDebug_)
    {
        DrawAIBotBehaviorTreeUI();
    }
    if (showNavGridDebug_)
    {
        DrawNavGridDebugOverlay();
    }

    return true;
}

void CharacterDemoGameInstance::DrawAdditionalPhysicsDebugOverlay(const Assets::Camera& camera) const
{
    NextGameplay::DrawCharacterControllerDebugOverlay(playerCharacter_.controller, camera);
    if (aiBot_.character.controller.IsValid())
    {
        NextGameplay::DrawCharacterControllerDebugOverlay(aiBot_.character.controller, camera);
    }
}

bool CharacterDemoGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    if (!playerCharacter_.controller.IsValid())
    {
        return false;
    }

    glm::vec3 charPos = playerCharacter_.controller.GetPosition();
    glm::vec3 forward = GetViewForward();
    glm::vec3 right = GetMoveRight();
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    glm::vec3 target(0.0f);
    glm::vec3 cameraPos(0.0f);

    if (firstPersonMode_)
    {
        cameraPos = GetEyePosition();
        target = cameraPos + forward;
        OutRenderCamera.FieldOfView = 75.0f;
    }
    else
    {
        target = charPos + glm::vec3(0.0f, config_.Camera.Height, 0.0f);
        cameraPos = target - forward * config_.Camera.Distance;
        OutRenderCamera.FieldOfView = 60.0f;
    }

    OutRenderCamera.ModelView = glm::lookAt(cameraPos, target, up);

    return true;
}

bool CharacterDemoGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    SDL_Keycode key = event.key.key;

    switch (key)
    {
    case SDLK_W: keyForward_ = pressed; return true;
    case SDLK_S: keyBack_ = pressed; return true;
    case SDLK_A: keyLeft_ = pressed; return true;
    case SDLK_D: keyRight_ = pressed; return true;
    case SDLK_SPACE:
        if (pressed) keyJump_ = true;
        return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        keySprint_ = pressed;
        return true;
    case SDLK_V:
        if (pressed)
        {
            SetFirstPersonMode(!firstPersonMode_);
        }
        return true;
    case SDLK_TAB:
        if (pressed)
        {
            movementMode_ = movementMode_ == ECharacterMovementMode::CameraAligned
                ? ECharacterMovementMode::MoveAligned
                : ECharacterMovementMode::CameraAligned;
            playerCharacter_.SetMovementMode(movementMode_);
            if (movementMode_ == ECharacterMovementMode::CameraAligned || firstPersonMode_)
            {
                characterYaw_ = yaw_;
            }
        }
        return true;
    case SDLK_F8:
        if (pressed)
        {
            showAIDebugMenu_ = !showAIDebugMenu_;
        }
        return true;
    case SDLK_0:
    case SDLK_KP_0:
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
    case SDLK_7:
    case SDLK_8:
    case SDLK_9:
    case SDLK_KP_1:
    case SDLK_KP_2:
    case SDLK_KP_3:
    case SDLK_KP_4:
    case SDLK_KP_5:
    case SDLK_KP_6:
    case SDLK_KP_7:
    case SDLK_KP_8:
    case SDLK_KP_9:
        if (showAIDebugMenu_)
        {
            if (!pressed)
            {
                return true;
            }

            switch (key)
            {
            case SDLK_1:
            case SDLK_KP_1:
                showBehaviorTreeDebug_ = !showBehaviorTreeDebug_;
                return true;
            case SDLK_2:
            case SDLK_KP_2:
                showNavGridDebug_ = !showNavGridDebug_;
                return true;
            case SDLK_0:
            case SDLK_KP_0:
                showAIDebugMenu_ = false;
                return true;
            default:
                return true;
            }
        }
        return false;
    case SDLK_ESCAPE:
        if (pressed)
        {
            mouseCaptured_ = !mouseCaptured_;
            resetMouse_ = true;
            SDL_SetWindowRelativeMouseMode(GetEngine().GetWindow().Handle(), mouseCaptured_);
        }
        return true;
    default:
        return false;
    }
}

bool CharacterDemoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (!mouseCaptured_)
    {
        return false;
    }

    if (SDL_GetWindowRelativeMouseMode(GetEngine().GetWindow().Handle()))
    {
        yaw_ -= static_cast<float>(xpos) * config_.Camera.MouseSensitivity;
        pitch_ += static_cast<float>(ypos) * config_.Camera.MouseSensitivity;
    }
    else
    {
        if (resetMouse_)
        {
            mousePos_ = glm::dvec2(xpos, ypos);
            resetMouse_ = false;
            return true;
        }

        const glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;
        mousePos_ = glm::dvec2(xpos, ypos);

        yaw_ -= static_cast<float>(delta.x) * config_.Camera.MouseSensitivity;
        pitch_ += static_cast<float>(delta.y) * config_.Camera.MouseSensitivity;
    }

    if (resetMouse_)
    {
        resetMouse_ = false;
        return true;
    }

    // Clamp pitch to avoid camera going exactly vertical
    constexpr float maxPitch = glm::radians(88.0f);
    pitch_ = glm::clamp(pitch_, -maxPitch, maxPitch);

    return true;
}

bool CharacterDemoGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        return false;
    }

    // Click to capture mouse if not captured
    if (!mouseCaptured_)
    {
        mouseCaptured_ = true;
        resetMouse_ = true;
        SDL_SetWindowRelativeMouseMode(GetEngine().GetWindow().Handle(), true);
        return true;
    }

    if (event.button.button == SDL_BUTTON_LEFT)
    {
        FireProjectile();
        return true;
    }

    return false;
}

bool CharacterDemoGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (firstPersonMode_)
    {
        return true;
    }

    // Zoom camera in/out
    config_.Camera.Distance -= static_cast<float>(yoffset) * config_.Camera.ScrollStep;
    config_.Camera.Distance = glm::clamp(config_.Camera.Distance, config_.Camera.MinDistance, config_.Camera.MaxDistance);
    return true;
}

glm::vec3 CharacterDemoGameInstance::GetMoveForward() const
{
    return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
}

glm::vec3 CharacterDemoGameInstance::GetMoveRight() const
{
    return glm::vec3(-std::cos(yaw_), 0.0f, std::sin(yaw_));
}

glm::vec3 CharacterDemoGameInstance::GetViewForward() const
{
    const float cosPitch = std::cos(pitch_);
    return glm::normalize(glm::vec3(
        std::sin(yaw_) * cosPitch,
        -std::sin(pitch_),
        std::cos(yaw_) * cosPitch));
}

glm::vec3 CharacterDemoGameInstance::GetEyePosition() const
{
    return playerCharacter_.controller.GetPosition() + glm::vec3(0.0f, config_.Player.EyeHeight, 0.0f);
}

glm::vec3 CharacterDemoGameInstance::GetAIBotEyePosition() const
{
    return aiBot_.character.controller.GetPosition() + glm::vec3(0.0f, config_.AI.EyeHeight, 0.0f);
}

float CharacterDemoGameInstance::GetCharacterYaw() const
{
    if (firstPersonMode_ || movementMode_ == ECharacterMovementMode::CameraAligned)
    {
        return yaw_;
    }
    return characterYaw_;
}

void CharacterDemoGameInstance::SetFirstPersonMode(bool enabled)
{
    firstPersonMode_ = enabled;
    playerCharacter_.SetFirstPersonMode(enabled);

    if (playerCharacter_.gameplay && playerCharacter_.gameplay->visualRoot)
    {
        Assets::NodeUtils::SetVisible(playerCharacter_.gameplay->visualRoot, !firstPersonMode_ && !playerCharacter_.modelLoaded);
    }

    SetNodeVisibilityRecursive(playerCharacter_.skinnedRoot, !firstPersonMode_);

    GetEngine().GetScene().MarkDirty();
}

void CharacterDemoGameInstance::FireProjectile()
{
    if (!playerCharacter_.controller.IsValid())
    {
        return;
    }

    const glm::vec3 shotDir = GetViewForward();
    const glm::vec3 spawnCenter = GetEyePosition() + shotDir * config_.Projectile.SpawnDistance;
    SpawnProjectile("ShotBox", spawnCenter, shotDir);
}

void CharacterDemoGameInstance::SpawnProjectile(const std::string& nodeName, const glm::vec3& spawnCenter,
                                                const glm::vec3& shotDir)
{
    if (glm::length(shotDir) <= 0.001f)
    {
        return;
    }

    const uint32_t instanceId = GetEngine().GetScene().GenerateInstanceId();
    auto newNode = Assets::SceneBuilder::CreateRenderNode(
        nodeName,
        spawnCenter,
        glm::vec3(1.0f),
        instanceId,
        projectileModelId_,
        projectileMatId_);

    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    NextBodyID bodyId = GetEngine().GetPhysicsEngine()->CreateBoxBody(
        spawnCenter,
        glm::vec3(config_.Projectile.Size),
        NextMotionType::Dynamic);
    phys->BindPhysicsBody(bodyId);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(bodyId, glm::normalize(shotDir) * config_.Projectile.Force);
}

void CharacterDemoGameInstance::UpdateCharacterNode()
{
    const glm::vec3 pos = playerCharacter_.controller.GetPosition();
    playerCharacter_.SyncTransform(pos, GetCharacterYaw());

    GetEngine().GetScene().MarkDirty();
}

void CharacterDemoGameInstance::InitAIBot()
{
    const glm::vec3 playerPosition = playerCharacter_.controller.GetPosition();

    aiBot_.character.Reset();
    aiBot_.visualNode.reset();

    aiBot_.agentComponent = std::make_shared<NextGameplay::AIAgentComponent>();
    auto& agent = GetAIBotAgent();
    agent.ResetRuntimeState(config_.AI.Enabled, playerPosition);
    agent.SetState(config_.AI.Enabled ? EAIBotState::Patrol : EAIBotState::Disabled);
    agent.SetDesiredState(agent.GetState());
    aiController_.Bind(
        &aiBot_.character,
        aiBot_.agentComponent.get(),
        &playerCharacter_,
        &navGrid_,
        &patrolRng_,
        CharacterDemoAIController::FCallbacks{
            .getPlayerEyePosition = [this]() { return GetEyePosition(); },
            .getAIBotEyePosition = [this]() { return GetAIBotEyePosition(); },
            .tryGetSceneNodePosition = [this](const std::string& nodeName, glm::vec3& outPosition)
            {
                return TryGetSceneNodePosition(nodeName, outPosition);
            },
            .hasLineOfSightToPlayer = [this]() { return HasLineOfSightToPlayer(); },
            .spawnProjectile = [this](const std::string& nodeName, const glm::vec3& spawnCenter, const glm::vec3& shotDir)
            {
                SpawnProjectile(nodeName, spawnCenter, shotDir);
            },
            .updateAnimationState = [this](float deltaSeconds) { UpdateAIBotAnimationState(deltaSeconds); },
            .updateNode = [this]() { UpdateAIBotNode(); },
        });
    aiController_.CollectPatrolPoints();

    glm::vec3 aiSpawn = playerPosition + glm::vec3(6.0f, 0.0f, 8.0f);
    if (!agent.patrolPoints.empty())
    {
        aiSpawn = agent.patrolPoints.front();
    }

    FCharacterControllerSettings settings;
    settings.height = 2.0f;
    settings.radius = 0.5f;
    settings.maxStrength = 4000.0f;
    settings.mass = 180.0f;
    settings.initialPosition = aiSpawn;

    NextGameplay::FCharacterActorSetup aiSetup;
    aiSetup.actorName = "EnemyBotActor";
    aiSetup.initialPosition = aiSpawn;
    aiSetup.appendRootName = characterAppendRootName_ + "_1";
    aiSetup.controlSource = NextGameplay::ECharacterControlSource::AI;
    aiSetup.movementMode = ECharacterMovementMode::MoveAligned;
    aiSetup.eyeHeight = config_.AI.EyeHeight;
    aiSetup.walkStrafePlaySpeed = config_.Animation.WalkStrafePlaySpeed;
    aiSetup.runBackwardPlaySpeed = config_.Animation.RunBackwardPlaySpeed;
    aiSetup.jumpStartHoldTime = config_.Animation.JumpStartHoldTime;
    aiSetup.jumpLandHoldTime = config_.Animation.JumpLandHoldTime;
    aiBot_.character.CreateController(GetEngine().GetPhysicsEngine(), settings);
    aiBot_.character.Initialize(GetEngine().GetScene(), aiSetup);
    aiBot_.character.actorRoot->AddComponent(aiBot_.agentComponent);
    agent.patrolProgressAnchor = aiSpawn;

    const glm::vec3 toPlayer = playerCharacter_.controller.GetPosition() - aiSpawn;
    const glm::vec3 lookDir = NormalizeHorizontalOrZero(toPlayer);
    if (glm::length(lookDir) > 0.001f)
    {
        agent.lookDir = lookDir;
        agent.yaw = std::atan2(lookDir.x, lookDir.z);
    }
    else
    {
        agent.yaw = 0.0f;
    }
    aiBot_.character.SetControlIntent(glm::vec3(0.0f), agent.lookDir, 0.0f, false, false);
    aiBot_.visualNode =
        aiBot_.character.CreatePlaceholderVisual(GetEngine().GetScene(), "EnemyBot", capsuleModelId_, aiCharacterMatId_);
}

bool CharacterDemoGameInstance::TryGetSceneNodePosition(const std::string& nodeName, glm::vec3& outPosition) const
{
    Assets::Node* node = GetEngine().GetScene().GetNode(nodeName);
    if (!node)
    {
        return false;
    }

    node->RecalcTransform(true);
    outPosition = node->WorldTranslation();
    return true;
}

bool CharacterDemoGameInstance::HasLineOfSightToPlayer() const
{
    if (!aiBot_.character.controller.IsValid() || !playerCharacter_.controller.IsValid())
    {
        return false;
    }

    const glm::vec3 origin = GetAIBotEyePosition();
    const glm::vec3 target = GetEyePosition();
    const glm::vec3 delta = target - origin;
    const float distance = glm::length(delta);
    if (distance <= 0.001f)
    {
        return true;
    }

    const Assets::RayCastResult hit =
        GetEngine().GetScene().GetCPUAccelerationStructure().RayCastInCPU(origin, delta / distance);
    if (!hit.Hitted)
    {
        return true;
    }

    return hit.T >= distance - 0.35f;
}

void CharacterDemoGameInstance::UpdateAIBot(float deltaSeconds)
{
    aiController_.Update(deltaSeconds, CreateAISettings());
}

void CharacterDemoGameInstance::UpdateAIBotNode()
{
    const auto& agent = GetAIBotAgent();
    const glm::vec3 position = aiBot_.character.controller.GetPosition();
    aiBot_.character.SyncTransform(position, agent.yaw);

    if (!aiBot_.character.actorRoot)
    {
        return;
    }
    GetEngine().GetScene().MarkDirty();
}

void CharacterDemoGameInstance::TryInitAIBotCharacterModel()
{
    if (!aiBot_.character.TryResolveSkinnedModel(GetEngine().GetScene(), characterAppendRootName_, 0,
                                                 playerCharacter_.skinnedRoot))
    {
        return;
    }

    const std::vector<std::string> animFiles = {
        "assets/models/characters/animations/Rig_Medium_General.glb",
        "assets/models/characters/animations/Rig_Medium_MovementBasic.glb",
        "assets/models/characters/animations/Rig_Medium_MovementAdvanced.glb",
    };

    for (const auto& animFile : animFiles)
    {
        std::vector<Assets::AnimationTrack> tracks;
        if (Assets::FSceneLoader::LoadAnimationTracks(animFile, tracks))
        {
            for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.character.skinnedMeshComps)
            {
                skinnedMeshComp->AddAnimations(tracks);
            }
        }
        else
        {
            SPDLOG_WARN("Failed to load AI animation file: {}", animFile);
        }
    }

    if (aiBot_.character.animation && aiBot_.character.primarySkinnedMeshComp)
    {
        aiBot_.character.animation->MapAnimationNames(aiBot_.character.primarySkinnedMeshComp->GetAnimationNames());
        if (playerCharacter_.animation && playerCharacter_.animation->GetIdleAnimationName().empty())
        {
            playerCharacter_.animation->MapAnimationNames(aiBot_.character.primarySkinnedMeshComp->GetAnimationNames());
        }
    }

    if (aiBot_.visualNode)
    {
        aiBot_.character.RemoveVisualRoot(GetEngine().GetScene(), GetEngine().GetPhysicsEngine());
        aiBot_.visualNode.reset();
    }

    aiBot_.character.AttachSkinnedModel(GetEngine().GetPhysicsEngine());

    if (aiBot_.character.animation && !aiBot_.character.animation->GetIdleAnimationName().empty())
    {
        aiBot_.character.PlayAnimation(aiBot_.character.animation->GetIdleAnimationName(), true);
    }

    aiBot_.character.SetModelLoaded(true);
    SPDLOG_INFO("AI character model initialized: root='{}', skinned meshes={}",
                aiBot_.character.skinnedRoot->GetName(), aiBot_.character.skinnedMeshComps.size());
}

void CharacterDemoGameInstance::UpdateAIBotAnimationState(float deltaSeconds)
{
    const auto& agent = GetAIBotAgent();

    if (!aiBot_.character.IsAnimationReady())
    {
        return;
    }

    NextGameplay::FCharacterAnimationUpdateInput input;
    input.velocity = aiBot_.character.controller.GetLinearVelocity();
    input.commandedMoveDir = aiBot_.character.control->GetMoveIntent();
    input.deltaSeconds = deltaSeconds;
    input.onGround = aiBot_.character.controller.IsOnGround();
    input.referenceForward = glm::vec3(std::sin(agent.yaw), 0.0f, std::cos(agent.yaw));
    input.referenceRight = glm::vec3(-input.referenceForward.z, 0.0f, input.referenceForward.x);
    input.desiredSpeed = aiBot_.character.control->GetDesiredSpeed();
    input.sprinting = aiBot_.character.control->GetSprinting();

    switch (agent.GetState())
    {
    case EAIBotState::Chase:
        input.policy = NextGameplay::ECharacterAnimationPolicy::AIChase;
        break;
    case EAIBotState::Evade:
        input.policy = NextGameplay::ECharacterAnimationPolicy::AIEvade;
        break;
    case EAIBotState::Attack:
        input.policy = NextGameplay::ECharacterAnimationPolicy::AIAttack;
        break;
    case EAIBotState::Patrol:
    default:
        input.policy = NextGameplay::ECharacterAnimationPolicy::AIPatrol;
        break;
    }

    aiBot_.character.UpdateAnimation(input);
}

void CharacterDemoGameInstance::TryInitCharacterModel()
{
    if (!playerCharacter_.TryResolveSkinnedModel(GetEngine().GetScene(), characterAppendRootName_, 0))
    {
        return; // Append root exists, but components are not fully attached yet.
    }

    SPDLOG_INFO("Character model root '{}' found with {} skinned mesh nodes, loading animation packs...",
                playerCharacter_.skinnedRoot->GetName(), playerCharacter_.skinnedMeshComps.size());

    // Load animation tracks from separate GLB files
    const std::vector<std::string> animFiles = {
        "assets/models/characters/animations/Rig_Medium_General.glb",
        "assets/models/characters/animations/Rig_Medium_MovementBasic.glb",
        "assets/models/characters/animations/Rig_Medium_MovementAdvanced.glb",
    };

    for (const auto& animFile : animFiles)
    {
        std::vector<Assets::AnimationTrack> tracks;
        if (Assets::FSceneLoader::LoadAnimationTracks(animFile, tracks))
        {
            SPDLOG_INFO("Loaded {} animation tracks from {}", tracks.size(), animFile);
            for (Runtime::SkinnedMeshComponent* skinnedMeshComp : playerCharacter_.skinnedMeshComps)
            {
                skinnedMeshComp->AddAnimations(tracks);
            }
        }
        else
        {
            SPDLOG_WARN("Failed to load animation file: {}", animFile);
        }
    }

    // Log and map all discovered animation names
    auto names = playerCharacter_.primarySkinnedMeshComp->GetAnimationNames();
    for (const auto& name : names)
    {
        SPDLOG_INFO("Character animation: '{}'", name);
    }
    if (playerCharacter_.animation)
    {
        playerCharacter_.animation->MapAnimationNames(names);
    }
    if (aiBot_.character.animation)
    {
        aiBot_.character.animation->MapAnimationNames(names);
    }

    // Remove the temporary placeholder once the real character is ready.
    playerCharacter_.RemoveVisualRoot(GetEngine().GetScene(), GetEngine().GetPhysicsEngine());

    // Character collision should come only from the controller, not the imported skinned mesh hierarchy.
    // Otherwise the appended mannequin can leave kinematic mesh colliders around the origin / T-pose.
    playerCharacter_.AttachSkinnedModel(GetEngine().GetPhysicsEngine());

    // Apply current first-person visibility and start idle animation
    SetFirstPersonMode(firstPersonMode_);

    if (playerCharacter_.animation && !playerCharacter_.animation->GetIdleAnimationName().empty())
    {
        PlayCharacterAnimation(playerCharacter_.animation->GetIdleAnimationName(), true);
    }

    playerCharacter_.SetModelLoaded(true);
    SPDLOG_INFO("Character model initialized: root='{}', animations={}",
                playerCharacter_.skinnedRoot->GetName(), names.size());
}

void CharacterDemoGameInstance::UpdateAnimationState(float deltaSeconds)
{
    if (!playerCharacter_.IsAnimationReady())
    {
        return;
    }

    NextGameplay::FCharacterAnimationUpdateInput input;
    input.velocity = playerCharacter_.controller.GetLinearVelocity();
    input.referenceForward = GetMoveForward();
    input.referenceRight = GetMoveRight();
    input.commandedMoveDir = playerCharacter_.control->GetMoveIntent();
    input.desiredSpeed = playerCharacter_.control->GetDesiredSpeed();
    input.deltaSeconds = deltaSeconds;
    input.onGround = playerCharacter_.controller.IsOnGround();
    input.sprinting = playerCharacter_.control->GetSprinting();
    input.policy =
        (!firstPersonMode_ && movementMode_ == ECharacterMovementMode::MoveAligned)
            ? NextGameplay::ECharacterAnimationPolicy::PlayerMoveAligned
            : NextGameplay::ECharacterAnimationPolicy::PlayerCameraRelative;

    playerCharacter_.UpdateAnimation(input);
}

void CharacterDemoGameInstance::ResetCharacterState()
{
    aiBot_.character.Reset();
    aiBot_.visualNode.reset();
    aiController_.Reset();
    if (aiBot_.agentComponent)
    {
        aiBot_.agentComponent->ResetRuntimeState(false, glm::vec3(0.0f));
        aiBot_.agentComponent->SetState(EAIBotState::Disabled);
        aiBot_.agentComponent->SetDesiredState(EAIBotState::Disabled);
    }
    aiBot_.agentComponent.reset();

    playerCharacter_.Reset();
    characterYaw_ = yaw_;
}

void CharacterDemoGameInstance::PlayCharacterAnimation(const std::string& name, bool loop, float playSpeed)
{
    if (name.empty())
    {
        return;
    }

    playerCharacter_.PlayAnimation(name, loop, playSpeed);
}

void CharacterDemoGameInstance::UpdateCharacterFacingYaw(const glm::vec3& moveDir,
                                                         const glm::vec3& currentVelocity,
                                                         float deltaSeconds)
{
    if (firstPersonMode_ || movementMode_ == ECharacterMovementMode::CameraAligned)
    {
        characterYaw_ = yaw_;
        return;
    }

    glm::vec2 horizontalVelocity(currentVelocity.x, currentVelocity.z);
    glm::vec2 desiredDirection(moveDir.x, moveDir.z);

    glm::vec2 facingDirection(0.0f);
    if (glm::length(horizontalVelocity) > 0.1f)
    {
        facingDirection = glm::normalize(horizontalVelocity);
    }
    else if (glm::length(desiredDirection) > 0.001f)
    {
        facingDirection = glm::normalize(desiredDirection);
    }

    if (glm::length(facingDirection) > 0.0f)
    {
        const float targetYaw = std::atan2(facingDirection.x, facingDirection.y);
        const float yawDelta = std::remainder(targetYaw - characterYaw_, glm::two_pi<float>());
        const float maxStep = config_.Player.CharacterTurnSpeed * deltaSeconds;

        if (std::abs(yawDelta) <= maxStep)
        {
            characterYaw_ = targetYaw;
        }
        else
        {
            characterYaw_ += glm::sign(yawDelta) * maxStep;
        }
    }
}

CharacterDemoAIDebugUI::FContext CharacterDemoGameInstance::CreateAIDebugUIContext() const
{
    return CharacterDemoAIDebugUI::FContext{
        .showAIDebugMenu = showAIDebugMenu_,
        .showBehaviorTreeDebug = showBehaviorTreeDebug_,
        .showNavGridDebug = showNavGridDebug_,
        .aiEnabled = config_.AI.Enabled,
        .agent = aiBot_.agentComponent.get(),
        .aiCharacter = &aiBot_.character,
        .playerCharacter = &playerCharacter_,
        .navGrid = &navGrid_,
        .getStateName = [this]() { return GetAIBotStateName(); },
        .hasLineOfSightToPlayer = [this]() { return HasLineOfSightToPlayer(); },
        .overrideRenderCamera = [this](Assets::Camera& camera) { return OverrideRenderCamera(camera); },
    };
}

void CharacterDemoGameInstance::DrawAIDebugMenu()
{
    CharacterDemoAIDebugUI::DrawMenu(CreateAIDebugUIContext());
}

void CharacterDemoGameInstance::DrawAIBotBehaviorTreeUI() const
{
    CharacterDemoAIDebugUI::DrawBehaviorTreeOverlay(CreateAIDebugUIContext());
}

const char* CharacterDemoGameInstance::GetAIBotStateName() const
{
    const auto* agent = aiBot_.agentComponent.get();
    const EAIBotState state = agent ? agent->GetState() : EAIBotState::Disabled;
    return CharacterDemoAIController::GetStateName(state);
}

const char* CharacterDemoGameInstance::GetAnimStateName() const
{
    return playerCharacter_.animation
        ? NextGameplay::GetCharacterAnimStateName(playerCharacter_.animation->GetAnimState())
        : "Unknown";
}

void CharacterDemoGameInstance::DrawNavGridDebugOverlay() const
{
    CharacterDemoAIDebugUI::DrawNavGridOverlay(CreateAIDebugUIContext());
}
