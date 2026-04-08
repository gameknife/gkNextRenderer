#include "CharacterDemoGameInstance.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "Assets/Core/Node.h"
#include "Assets/Loaders/FProcModel.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "NextGameplay/Gameplay/GameplayMath.hpp"
#include "NextGameplay/Reflection/GameplayReflectionRegistry.h"
#include "NextGameplay/Utilities/SceneNodeUtils.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Utilities/PhysicsDebugOverlay.hpp"
#include "Vulkan/WindowSurface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<CharacterDemoGameInstance>(config, options, engine);
}

using NextGameplay::AdvanceYawToward;
using NextGameplay::CollectSkinnedMeshComponents;
using NextGameplay::DisableNodePhysicsRecursive;
using NextGameplay::FindAppendedCharacterRoot;
using NextGameplay::NormalizeHorizontalOrZero;
using NextGameplay::SetNodeRayCastVisibilityRecursive;
using NextGameplay::SetNodeVisibilityRecursive;

CharacterDemoGameInstance::CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
    // config.Height = 720;
    // config.Width = 1280;
}

void CharacterDemoGameInstance::OnInit()
{
    NextGameplay::RegisterGameplayReflection();

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
    engine_->RequestLoadScene(initialScene);
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

    float speed = keySprint_ ? runSpeed_ : walkSpeed_;
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

void CharacterDemoGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.superResolution", "4", &error);
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
    materials.push_back({Assets::Material::Lambertian(glm::vec3(0.2f, 0.8f, 0.3f))});
    characterMatId_ = static_cast<uint32_t>(materials.size() - 1);

    materials.push_back({Assets::Material::Lambertian(glm::vec3(0.9f, 0.25f, 0.2f))});
    aiCharacterMatId_ = static_cast<uint32_t>(materials.size() - 1);

    const float halfProjectile = projectileSize_ * 0.5f;
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
    const auto& cam = engine_->GetScene().GetRenderCamera();
    glm::mat4 invModelView = glm::inverse(cam.ModelView);
    glm::vec3 camPos = glm::vec3(invModelView[3]);
    settings.initialPosition = glm::vec3(camPos.x, camPos.y, camPos.z);

    playerCharacter_.CreateController(engine_->GetPhysicsEngine(), settings);

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
    playerSetup.footIKEnabled = footIKEnabled_;
    playerSetup.eyeHeight = firstPersonEyeHeight_;
    playerSetup.facingYaw = characterYaw_;
    playerSetup.walkStrafePlaySpeed = walkStrafePlaySpeed_;
    playerSetup.runBackwardPlaySpeed = runBackwardPlaySpeed_;
    playerSetup.jumpStartHoldTime = jumpStartHoldTime_;
    playerSetup.jumpLandHoldTime = jumpLandHoldTime_;
    playerCharacter_.Initialize(engine_->GetScene(), playerSetup);
    playerCharacter_.SetControlIntent(glm::vec3(0.0f), GetViewForward(), 0.0f, false, false);
    playerCharacter_.CreatePlaceholderVisual(engine_->GetScene(), "CharacterBody", capsuleModelId_, characterMatId_);
    InitAIBot();

    // Build navigation grid from scene BVH for AI pathfinding
    {
        const FNavGridSettings navSettings = CreateNavGridSettings();
        navGrid_.Build(engine_->GetScene().GetCPUAccelerationStructure(), navSettings);
        engine_->GetScene().GetCPUAccelerationStructure().ClearNavRelevantDirtyBounds();
    }

    SetFirstPersonMode(firstPersonMode_);
    engine_->GetScene().MarkDirty();

    engine_->GetScene().GetEnvSettings().SkyIdx = 2;
    
    // Load the skinned character model asynchronously
    engine_->RequestLoadSceneAdd("assets/models/characters/Mannequin_Medium.glb");
    playerCharacter_.SetModelLoadRequested(true);
    engine_->RequestLoadSceneAdd("assets/models/characters/Mannequin_Medium.glb");
    aiBot_.character.SetModelLoadRequested(true);

    // Capture mouse
    mouseCaptured_ = true;
    resetMouse_ = true;
    SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), true);
}

void CharacterDemoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();
    ResetCharacterState();
    sceneHelpersInjected_ = false;
}

CharacterDemoGameInstance::FNavGridSettings CharacterDemoGameInstance::CreateNavGridSettings() const
{
    const glm::vec3 sceneMin = engine_->GetScene().GetSceneAABBMin();
    const glm::vec3 sceneMax = engine_->GetScene().GetSceneAABBMax();

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

void CharacterDemoGameInstance::RefreshNavGridFromSceneDirtyRegion()
{
    glm::vec3 dirtyWorldMin(0.0f);
    glm::vec3 dirtyWorldMax(0.0f);
    auto& cpuAS = engine_->GetScene().GetCPUAccelerationStructure();
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

    aiBot_.pathFollower.Clear();
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
    ImGui::Text("Move Mode: %s", GetMovementModeName());
    ImGui::Text("Graphics Debug: %s", engine_->IsGraphicsDebugPanelVisible() ? "On" : "Off");
    ImGui::Text("Physics Debug: %s", showPhysicsDebug_ ? "On" : "Off");
    ImGui::Text("Foot IK: %s", footIKEnabled_ ? "On" : "Off");
    ImGui::Text("Foot IK Debug: %s", showFootIKDebug_ ? "On" : "Off");
    ImGui::Text("AI Debug Menu: %s", showAIDebugMenu_ ? "On" : "Off");
    ImGui::Text("AI BT Overlay: %s", showBehaviorTreeDebug_ ? "On" : "Off");
    ImGui::Text("NavGrid Overlay: %s", showNavGridDebug_ ? "On" : "Off");
    ImGui::Text("AI: %s", aiEnabled_ ? GetAIBotStateName() : "Disabled");
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
        const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
        const float botDistance = glm::distance(aiPos, pos);
        ImGui::Text("AI Pos: %.1f, %.1f, %.1f", aiPos.x, aiPos.y, aiPos.z);
        ImGui::Text("AI Dist: %.1f | Visible: %s | LOS: %s", botDistance, aiBot_.targetVisible ? "Yes" : "No",
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
    ImGui::Text("LMB - Shoot | F1 - Physics | F2 - Graphics | F3 - Foot IK | Q - Next Renderer");
    ImGui::Text("1-8 - View Modes | F8 - AI Debug Menu | F9 - IK Debug");
    ImGui::Text("ESC - Release Mouse");

    ImGui::SliderFloat("Walk Speed", &walkSpeed_, 1.0f, 10.0f);
    ImGui::SliderFloat("Run Speed", &runSpeed_, 5.0f, 20.0f);
    ImGui::SliderFloat("Camera Dist", &cameraDistance_, 1.0f, 15.0f);
    if (ImGui::Checkbox("Enable AI", &aiEnabled_))
    {
        if (aiBot_.agentComponent)
        {
            aiBot_.agentComponent->SetEnabled(aiEnabled_);
        }
    }
    ImGui::SliderFloat("AI Sight", &aiSightRange_, 8.0f, 60.0f);
    ImGui::SliderFloat("AI Fire Range", &aiFireRange_, 4.0f, 40.0f);
    ImGui::SliderFloat("AI Fire Cooldown", &aiFireCooldown_, 0.2f, 4.0f);

    ImGui::End();

    if (showAIDebugMenu_)
    {
        DrawAIDebugMenu();
    }

    if (showBehaviorTreeDebug_)
    {
        DrawAIBotBehaviorTreeUI();
    }
    if (showPhysicsDebug_)
    {
        Assets::Camera debugCamera = engine_->GetScene().GetRenderCamera();
        OverrideRenderCamera(debugCamera);
        Runtime::DrawPhysicsDebugOverlay(engine_->GetScene(), debugCamera);
        Runtime::DrawCharacterControllerDebugOverlay(playerCharacter_.controller, debugCamera);
        if (aiBot_.character.controller.IsValid())
        {
            Runtime::DrawCharacterControllerDebugOverlay(aiBot_.character.controller, debugCamera);
        }
    }

    if (showNavGridDebug_)
    {
        DrawNavGridDebugOverlay();
    }

    return true;
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
        target = charPos + glm::vec3(0.0f, cameraHeight_, 0.0f);
        cameraPos = target - forward * cameraDistance_;
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
    case SDLK_F1:
        if (pressed)
        {
            showPhysicsDebug_ = !showPhysicsDebug_;
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
    case SDLK_F9:
        if (pressed)
        {
            showFootIKDebug_ = !showFootIKDebug_;
        }
        return true;
    case SDLK_F3:
        if (pressed)
        {
            footIKEnabled_ = !footIKEnabled_;
            playerCharacter_.SetFootIKEnabled(footIKEnabled_);
            aiBot_.character.SetFootIKEnabled(footIKEnabled_);
        }
        return true;
    case SDLK_ESCAPE:
        if (pressed)
        {
            mouseCaptured_ = !mouseCaptured_;
            resetMouse_ = true;
            SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), mouseCaptured_);
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

    if (SDL_GetWindowRelativeMouseMode(engine_->GetWindow().Handle()))
    {
        yaw_ -= static_cast<float>(xpos) * mouseSensitivity_;
        pitch_ += static_cast<float>(ypos) * mouseSensitivity_;
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

        yaw_ -= static_cast<float>(delta.x) * mouseSensitivity_;
        pitch_ += static_cast<float>(delta.y) * mouseSensitivity_;
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
        SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), true);
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
    cameraDistance_ -= static_cast<float>(yoffset) * 0.5f;
    cameraDistance_ = glm::clamp(cameraDistance_, 1.0f, 20.0f);
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
    return playerCharacter_.controller.GetPosition() + glm::vec3(0.0f, firstPersonEyeHeight_, 0.0f);
}

glm::vec3 CharacterDemoGameInstance::GetAIBotEyePosition() const
{
    return aiBot_.character.controller.GetPosition() + glm::vec3(0.0f, aiEyeHeight_, 0.0f);
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
        if (auto renderComp = playerCharacter_.gameplay->visualRoot->GetComponent<Runtime::RenderComponent>())
        {
            renderComp->SetVisible(!firstPersonMode_ && !playerCharacter_.modelLoaded);
        }
    }

    SetNodeVisibilityRecursive(playerCharacter_.skinnedRoot, !firstPersonMode_);

    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::FireProjectile()
{
    if (!playerCharacter_.controller.IsValid())
    {
        return;
    }

    const glm::vec3 shotDir = GetViewForward();
    const glm::vec3 spawnCenter = GetEyePosition() + shotDir * projectileSpawnDistance_;
    SpawnProjectile("ShotBox", spawnCenter, shotDir);
}

void CharacterDemoGameInstance::SpawnProjectile(const std::string& nodeName, const glm::vec3& spawnCenter,
                                                const glm::vec3& shotDir)
{
    if (glm::length(shotDir) <= 0.001f)
    {
        return;
    }

    const uint32_t instanceId = engine_->GetScene().GenerateInstanceId();
    auto newNode = Assets::Node::CreateNode(
        nodeName,
        spawnCenter,
        glm::quat(1, 0, 0, 0),
        glm::vec3(1.0f),
        instanceId);

    auto renderComp = std::make_shared<Runtime::RenderComponent>();
    renderComp->SetModelId(projectileModelId_);
    renderComp->SetMaterial({projectileMatId_});
    renderComp->SetVisible(true);
    newNode->AddComponent(renderComp);

    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    NextBodyID bodyId = engine_->GetPhysicsEngine()->CreateBoxBody(
        spawnCenter,
        glm::vec3(projectileSize_),
        NextMotionType::Dynamic);
    phys->BindPhysicsBody(bodyId);
    newNode->AddComponent(phys);

    engine_->GetScene().AddNode(newNode);
    engine_->GetScene().MarkDirty();

    engine_->GetPhysicsEngine()->AddForceToBody(bodyId, glm::normalize(shotDir) * projectileForce_);
}

void CharacterDemoGameInstance::UpdateCharacterNode()
{
    const glm::vec3 pos = playerCharacter_.controller.GetPosition();
    playerCharacter_.SyncTransform(pos, GetCharacterYaw());

    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::InitAIBot()
{
    aiBot_.character.Reset();
    aiBot_.visualNode.reset();
    aiBot_.patrolPoints.clear();
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.lookDir = glm::vec3(0.0f, 0.0f, 1.0f);
    aiBot_.lastKnownTargetPosition = playerCharacter_.controller.GetPosition();
    aiBot_.fireCooldownRemaining = 0.0f;
    aiBot_.targetMemoryRemaining = 0.0f;
    aiBot_.patrolPauseRemaining = 0.0f;
    aiBot_.targetVisibleGraceRemaining = 0.0f;
    aiBot_.stateHoldRemaining = 0.0f;
    aiBot_.strafeSign = 1.0f;
    aiBot_.patrolIndex = 0;
    aiBot_.targetVisible = false;
    aiBot_.triggerJump = false;
    aiBot_.state = aiEnabled_ ? EAIBotState::Patrol : EAIBotState::Disabled;
    aiBot_.desiredState = aiBot_.state;
    aiBot_.patrolReachableFound = false;
    aiBot_.patrolUsedNearFallback = false;
    aiBot_.patrolRequestedIndex = 0;
    aiBot_.patrolSelectedIndex = 0;
    aiBot_.patrolCandidatesTested = 0;
    aiBot_.patrolWaypointCount = 0;
    aiBot_.patrolSelectionMs = 0.0f;
    aiBot_.patrolSelectedDistance = 0.0f;
    aiBot_.patrolStuckTime = 0.0f;
    aiBot_.patrolAbandonedTarget = false;
    aiBot_.patrolLastCommittedIndex = std::numeric_limits<size_t>::max();
    aiBot_.patrolSelectedTarget = glm::vec3(0.0f);
    aiBot_.patrolProgressAnchor = glm::vec3(0.0f);

    aiBot_.agentComponent = std::make_shared<NextGameplay::AIAgentComponent>();
    aiBot_.agentComponent->ResetRuntimeState(aiEnabled_, playerCharacter_.controller.GetPosition());
    aiBot_.agentComponent->SetState(aiEnabled_ ? EAIBotState::Patrol : EAIBotState::Disabled);
    aiBot_.agentComponent->SetDesiredState(aiBot_.agentComponent->GetState());

    CollectAIBotPatrolPoints();

    glm::vec3 aiSpawn = playerCharacter_.controller.GetPosition() + glm::vec3(6.0f, 0.0f, 8.0f);
    if (!aiBot_.patrolPoints.empty())
    {
        aiSpawn = aiBot_.patrolPoints.front();
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
    aiSetup.footIKEnabled = footIKEnabled_;
    aiSetup.eyeHeight = aiEyeHeight_;
    aiSetup.walkStrafePlaySpeed = walkStrafePlaySpeed_;
    aiSetup.runBackwardPlaySpeed = runBackwardPlaySpeed_;
    aiSetup.jumpStartHoldTime = jumpStartHoldTime_;
    aiSetup.jumpLandHoldTime = jumpLandHoldTime_;
    aiBot_.character.CreateController(engine_->GetPhysicsEngine(), settings);
    aiBot_.character.Initialize(engine_->GetScene(), aiSetup);
    aiBot_.character.actorRoot->AddComponent(aiBot_.agentComponent);
    aiBot_.patrolProgressAnchor = aiSpawn;

    const glm::vec3 toPlayer = playerCharacter_.controller.GetPosition() - aiSpawn;
    const glm::vec3 lookDir = NormalizeHorizontalOrZero(toPlayer);
    if (glm::length(lookDir) > 0.001f)
    {
        aiBot_.lookDir = lookDir;
        aiBot_.yaw = std::atan2(lookDir.x, lookDir.z);
    }
    else
    {
        aiBot_.yaw = 0.0f;
    }
    aiBot_.character.SetControlIntent(glm::vec3(0.0f), aiBot_.lookDir, 0.0f, false, false);
    aiBot_.visualNode =
        aiBot_.character.CreatePlaceholderVisual(engine_->GetScene(), "EnemyBot", capsuleModelId_, aiCharacterMatId_);
}

void CharacterDemoGameInstance::CollectAIBotPatrolPoints()
{
    aiBot_.patrolPoints.clear();

    const std::array<std::string, 6> patrolNodeNames{
        "WarmupPad_Left",
        "WarmupPad_Right",
        "Connector_LeftBranch",
        "Connector_RightBranch",
        "Connector_BackLeft",
        "Connector_BackRight",
    };

    for (const std::string& nodeName : patrolNodeNames)
    {
        glm::vec3 position(0.0f);
        if (!TryGetSceneNodePosition(nodeName, position))
        {
            continue;
        }

        bool duplicate = false;
        for (const glm::vec3& existing : aiBot_.patrolPoints)
        {
            if (glm::distance(existing, position) < 0.5f)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
        {
            aiBot_.patrolPoints.push_back(position);
        }
    }

    if (aiBot_.patrolPoints.empty())
    {
        aiBot_.patrolPoints = {
            glm::vec3(-6.0f, 0.0f, 6.0f),
            glm::vec3(6.0f, 0.0f, 6.0f),
            glm::vec3(-12.0f, 0.0f, 18.0f),
            glm::vec3(12.0f, 0.0f, 18.0f),
        };
    }

    if (aiBot_.agentComponent)
    {
        aiBot_.agentComponent->patrolPoints = aiBot_.patrolPoints;
    }
}

bool CharacterDemoGameInstance::TryBuildReachablePatrolPath(const glm::vec3& currentPos, float referenceHeight,
                                                            size_t startIndex, size_t& outPatrolIndex,
                                                            glm::vec3& outTarget,
                                                            std::vector<glm::vec3>& outPath)
{
    outPath.clear();
    outTarget = currentPos;

    aiBot_.patrolReachableFound = false;
    aiBot_.patrolUsedNearFallback = false;
    aiBot_.patrolRequestedIndex = startIndex;
    aiBot_.patrolSelectedIndex = startIndex;
    aiBot_.patrolCandidatesTested = 0;
    aiBot_.patrolWaypointCount = 0;
    aiBot_.patrolSelectionMs = 0.0f;
    aiBot_.patrolSelectedDistance = 0.0f;
    aiBot_.patrolSelectedTarget = currentPos;

    if (aiBot_.patrolPoints.empty() || !navGrid_.IsBuilt())
    {
        return false;
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    const size_t patrolCount = aiBot_.patrolPoints.size();
    const float minTravelDistance = std::max(aiPatrolMinTravelDistance_, aiPatrolPointRadius_ * 2.0f);
    const std::vector<uint8_t> reachableMask = navGrid_.BuildReachabilityMask(currentPos, referenceHeight);
    const int gridWidth = navGrid_.GetWidth();
    const int gridHeight = navGrid_.GetHeight();

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
        std::shuffle(candidateOrder.begin(), candidateOrder.end(), patrolRng_);
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
            const glm::vec3& candidateTarget = aiBot_.patrolPoints[candidateIndex];
            ++aiBot_.patrolCandidatesTested;

            if (avoidLastCommitted && patrolCount > 1 && candidateIndex == aiBot_.patrolLastCommittedIndex)
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

                    const glm::vec3 cellWorld = navGrid_.GetCellWorldPosition(gx, gz);
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

            std::vector<glm::vec3> candidatePath = navGrid_.FindPath(currentPos, bestReachableTarget, referenceHeight);
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
        FPatrolCandidateOption selected = std::move(candidates[distribution(patrolRng_)]);

        outPatrolIndex = selected.patrolIndex;
        outTarget = selected.resolvedTarget;
        outPath = std::move(selected.path);
        aiBot_.patrolReachableFound = true;
        aiBot_.patrolUsedNearFallback = usedNearFallback;
        aiBot_.patrolSelectedIndex = selected.patrolIndex;
        aiBot_.patrolWaypointCount = static_cast<int>(outPath.size());
        aiBot_.patrolSelectedDistance = selected.travelDistance;
        aiBot_.patrolSelectedTarget = selected.resolvedTarget;
        aiBot_.patrolLastCommittedIndex = selected.patrolIndex;
        aiBot_.patrolSelectionMs =
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

    aiBot_.patrolSelectionMs =
        std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - startTime).count();
    auto candidates = collectCandidates(false, false);
    return commitCandidate(candidates, true);
}

bool CharacterDemoGameInstance::TryGetSceneNodePosition(const std::string& nodeName, glm::vec3& outPosition) const
{
    Assets::Node* node = engine_->GetScene().GetNode(nodeName);
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
        engine_->GetScene().GetCPUAccelerationStructure().RayCastInCPU(origin, delta / distance);
    if (!hit.Hitted)
    {
        return true;
    }

    return hit.T >= distance - 0.35f;
}

CharacterDemoGameInstance::EAIBotState CharacterDemoGameInstance::DetermineDesiredAIBotState(
    float distanceToPlayer, bool hasCombatTarget) const
{
    const bool hasChaseTarget = hasCombatTarget || aiBot_.targetMemoryRemaining > 0.0f;
    const bool hasRecentCombatTarget = hasCombatTarget || aiBot_.targetVisibleGraceRemaining > 0.0f ||
                                       aiBot_.targetMemoryRemaining > 0.0f;
    if (!hasChaseTarget)
    {
        return aiBot_.patrolPoints.empty() ? EAIBotState::Disabled : EAIBotState::Patrol;
    }

    const float hysteresis = aiCombatRangeHysteresis_;
    const float evadeEnter = aiPreferredCombatRangeMin_;
    const float evadeExit = aiPreferredCombatRangeMin_ + hysteresis;
    const float attackEnterMax =
        std::min(aiFireRange_, std::max(aiPreferredCombatRangeMin_, aiPreferredCombatRangeMax_ - hysteresis * 0.5f));
    const float attackMinSticky = std::max(0.0f, aiPreferredCombatRangeMin_ - hysteresis);
    const float attackMaxSticky = std::min(aiFireRange_, aiPreferredCombatRangeMax_ + hysteresis);

    switch (aiBot_.previousState)
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

void CharacterDemoGameInstance::UpdateAIBot(float deltaSeconds)
{
    if (!aiBot_.character.controller.IsValid())
    {
        aiBot_.behaviorRootStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorAttackStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorChaseStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        if (aiBot_.agentComponent)
        {
            aiBot_.agentComponent->ResetDebugState();
        }
        return;
    }

    aiBot_.fireCooldownRemaining = std::max(0.0f, aiBot_.fireCooldownRemaining - deltaSeconds);
    aiBot_.targetMemoryRemaining = std::max(0.0f, aiBot_.targetMemoryRemaining - deltaSeconds);
    aiBot_.patrolPauseRemaining = std::max(0.0f, aiBot_.patrolPauseRemaining - deltaSeconds);
    aiBot_.targetVisibleGraceRemaining = std::max(0.0f, aiBot_.targetVisibleGraceRemaining - deltaSeconds);
    aiBot_.stateHoldRemaining = std::max(0.0f, aiBot_.stateHoldRemaining - deltaSeconds);
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.triggerJump = false;
    aiBot_.character.ClearControlFrameState();
    aiBot_.character.SetControlIntent(glm::vec3(0.0f), aiBot_.lookDir, 0.0f, false, false);

    const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
    const glm::vec3 playerPos = playerCharacter_.controller.GetPosition();
    const glm::vec3 toPlayer = playerPos - aiPos;
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
    const glm::vec3 botForward(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    const float fovDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool closeThreat = distanceToPlayer <= aiPreferredCombatRangeMin_;
    const bool hasLineOfSight = closeThreat || HasLineOfSightToPlayer();
    const float sightRange = aiBot_.targetVisible ? aiLoseSightRange_ : aiSightRange_;
    const bool rawTargetVisible =
        aiEnabled_ &&
        distanceToPlayer <= sightRange &&
        std::abs(toPlayer.y) <= 4.0f &&
        hasLineOfSight &&
        (closeThreat || fovDot >= std::cos(glm::radians(75.0f)));

    if (rawTargetVisible)
    {
        aiBot_.lastKnownTargetPosition = playerPos;
        aiBot_.targetMemoryRemaining = aiMemoryTime_;
        aiBot_.targetVisibleGraceRemaining = aiTargetVisibleGraceTime_;
    }

    aiBot_.targetVisible = rawTargetVisible || aiBot_.targetVisibleGraceRemaining > 0.0f;
    if (aiBot_.agentComponent)
    {
        aiBot_.agentComponent->SetEnabled(aiEnabled_);
        aiBot_.agentComponent->SetTargetVisible(aiBot_.targetVisible);
        aiBot_.agentComponent->lookDir = aiBot_.character.control ? aiBot_.character.control->GetLookIntent() : aiBot_.lookDir;
        aiBot_.agentComponent->moveDir = aiBot_.character.control ? aiBot_.character.control->GetMoveIntent() : aiBot_.moveDir;
        aiBot_.agentComponent->lastKnownTargetPosition = aiBot_.lastKnownTargetPosition;
        aiBot_.agentComponent->yaw = aiBot_.yaw;
    }

    if (!aiEnabled_)
    {
        aiBot_.state = EAIBotState::Disabled;
        aiBot_.desiredState = EAIBotState::Disabled;
        aiBot_.behaviorRootStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorAttackStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorChaseStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        aiBot_.character.SetControlIntent(glm::vec3(0.0f), aiBot_.lookDir, 0.0f, false, false);
        aiBot_.character.controller.Update(aiBot_.character.control ? aiBot_.character.control->GetMoveIntent() : glm::vec3(0.0f),
                                           aiBot_.character.control ? aiBot_.character.control->GetDesiredSpeed() : 0.0f,
                                           aiBot_.character.ConsumeJumpRequested(), deltaSeconds);
        UpdateAIBotAnimationState(deltaSeconds);
        UpdateAIBotNode();
        if (aiBot_.agentComponent)
        {
            aiBot_.agentComponent->SetState(EAIBotState::Disabled);
            aiBot_.agentComponent->SetDesiredState(EAIBotState::Disabled);
            aiBot_.agentComponent->ResetDebugState();
        }
        return;
    }

    aiBot_.previousState = aiBot_.state;
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
    aiBot_.desiredState = DetermineDesiredAIBotState(distanceToPlayer, aiBot_.targetVisible);
    if (aiBot_.desiredState != aiBot_.state &&
        aiBot_.stateHoldRemaining > 0.0f &&
        getStatePriority(aiBot_.desiredState) <= getStatePriority(aiBot_.state))
    {
        aiBot_.desiredState = aiBot_.state;
    }

    const EAIBotState stateBefore = aiBot_.state;
    RunAIBotBehaviorTree(deltaSeconds);
    if (aiBot_.state != stateBefore)
    {
        aiBot_.pathFollower.Clear();
        aiBot_.stateHoldRemaining = aiStateMinHoldTime_;
    }

    float speed = aiWalkSpeed_;
    if (aiBot_.state == EAIBotState::Chase)
    {
        speed = aiRunSpeed_;
    }
    else if (aiBot_.state == EAIBotState::Evade)
    {
        speed = aiRunSpeed_;
    }
    else if (aiBot_.state == EAIBotState::Attack && distanceToPlayer > aiPreferredCombatRangeMax_)
    {
        speed = aiRunSpeed_;
    }

    aiBot_.character.SetControlIntent(aiBot_.moveDir, aiBot_.lookDir, speed, speed > aiWalkSpeed_ + 0.05f,
                                      aiBot_.triggerJump);

    aiBot_.character.controller.Update(aiBot_.character.control ? aiBot_.character.control->GetMoveIntent() : aiBot_.moveDir,
                                       aiBot_.character.control ? aiBot_.character.control->GetDesiredSpeed() : speed,
                                       aiBot_.character.ConsumeJumpRequested(), deltaSeconds);

    glm::vec3 facingDirection = aiBot_.character.control ? aiBot_.character.control->GetLookIntent() : aiBot_.lookDir;
    if (aiBot_.state != EAIBotState::Attack &&
        glm::length(aiBot_.character.controller.GetLinearVelocity()) > 0.2f)
    {
        facingDirection = aiBot_.character.controller.GetLinearVelocity();
    }
    aiBot_.yaw = AdvanceYawToward(aiBot_.yaw, facingDirection, aiTurnSpeed_, deltaSeconds);

    UpdateAIBotAnimationState(deltaSeconds);
    UpdateAIBotNode();
    if (aiBot_.agentComponent)
    {
        aiBot_.agentComponent->SetState(aiBot_.state);
        aiBot_.agentComponent->SetDesiredState(aiBot_.desiredState);
        aiBot_.agentComponent->SetPreviousState(aiBot_.previousState);
        aiBot_.agentComponent->SetTargetVisible(aiBot_.targetVisible);
        aiBot_.agentComponent->SetBehaviorRootStatus(aiBot_.behaviorRootStatus);
        aiBot_.agentComponent->SetBehaviorEvadeStatus(aiBot_.behaviorEvadeStatus);
        aiBot_.agentComponent->SetBehaviorAttackStatus(aiBot_.behaviorAttackStatus);
        aiBot_.agentComponent->SetBehaviorChaseStatus(aiBot_.behaviorChaseStatus);
        aiBot_.agentComponent->SetBehaviorPatrolStatus(aiBot_.behaviorPatrolStatus);
        aiBot_.agentComponent->moveDir = aiBot_.character.control ? aiBot_.character.control->GetMoveIntent() : aiBot_.moveDir;
        aiBot_.agentComponent->lookDir = aiBot_.character.control ? aiBot_.character.control->GetLookIntent() : aiBot_.lookDir;
        aiBot_.agentComponent->lastKnownTargetPosition = aiBot_.lastKnownTargetPosition;
        aiBot_.agentComponent->yaw = aiBot_.yaw;
        aiBot_.agentComponent->fireCooldownRemaining = aiBot_.fireCooldownRemaining;
        aiBot_.agentComponent->targetMemoryRemaining = aiBot_.targetMemoryRemaining;
        aiBot_.agentComponent->patrolPauseRemaining = aiBot_.patrolPauseRemaining;
        aiBot_.agentComponent->targetVisibleGraceRemaining = aiBot_.targetVisibleGraceRemaining;
        aiBot_.agentComponent->stateHoldRemaining = aiBot_.stateHoldRemaining;
        aiBot_.agentComponent->strafeSign = aiBot_.strafeSign;
        aiBot_.agentComponent->patrolIndex = aiBot_.patrolIndex;
        aiBot_.agentComponent->triggerJump = aiBot_.triggerJump;
        aiBot_.agentComponent->patrolReachableFound = aiBot_.patrolReachableFound;
        aiBot_.agentComponent->patrolUsedNearFallback = aiBot_.patrolUsedNearFallback;
        aiBot_.agentComponent->patrolRequestedIndex = aiBot_.patrolRequestedIndex;
        aiBot_.agentComponent->patrolSelectedIndex = aiBot_.patrolSelectedIndex;
        aiBot_.agentComponent->patrolCandidatesTested = aiBot_.patrolCandidatesTested;
        aiBot_.agentComponent->patrolWaypointCount = aiBot_.patrolWaypointCount;
        aiBot_.agentComponent->patrolSelectionMs = aiBot_.patrolSelectionMs;
        aiBot_.agentComponent->patrolSelectedDistance = aiBot_.patrolSelectedDistance;
        aiBot_.agentComponent->patrolStuckTime = aiBot_.patrolStuckTime;
        aiBot_.agentComponent->patrolAbandonedTarget = aiBot_.patrolAbandonedTarget;
        aiBot_.agentComponent->patrolLastCommittedIndex = aiBot_.patrolLastCommittedIndex;
        aiBot_.agentComponent->patrolSelectedTarget = aiBot_.patrolSelectedTarget;
        aiBot_.agentComponent->patrolProgressAnchor = aiBot_.patrolProgressAnchor;
        aiBot_.agentComponent->pathFollower = aiBot_.pathFollower;
    }
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotBehaviorTree(float deltaSeconds)
{
    aiBot_.behaviorRootStatus = EBehaviorDebugState::Running;
    aiBot_.behaviorEvadeStatus = EBehaviorDebugState::Inactive;
    aiBot_.behaviorAttackStatus = EBehaviorDebugState::Inactive;
    aiBot_.behaviorChaseStatus = EBehaviorDebugState::Inactive;
    aiBot_.behaviorPatrolStatus = EBehaviorDebugState::Inactive;

    const EBehaviorTreeStatus evadeStatus = RunAIBotEvade(deltaSeconds);
    aiBot_.behaviorEvadeStatus = ToBehaviorDebugState(evadeStatus);
    if (evadeStatus != EBehaviorTreeStatus::Failure)
    {
        aiBot_.behaviorRootStatus = ToBehaviorDebugState(evadeStatus);
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus attackStatus = RunAIBotAttack(deltaSeconds);
    aiBot_.behaviorAttackStatus = ToBehaviorDebugState(attackStatus);
    if (attackStatus != EBehaviorTreeStatus::Failure)
    {
        aiBot_.behaviorRootStatus = ToBehaviorDebugState(attackStatus);
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus chaseStatus = RunAIBotChase(deltaSeconds);
    aiBot_.behaviorChaseStatus = ToBehaviorDebugState(chaseStatus);
    if (chaseStatus != EBehaviorTreeStatus::Failure)
    {
        aiBot_.behaviorRootStatus = ToBehaviorDebugState(chaseStatus);
        return EBehaviorTreeStatus::Running;
    }

    const EBehaviorTreeStatus patrolStatus = RunAIBotPatrol(deltaSeconds);
    aiBot_.behaviorPatrolStatus = ToBehaviorDebugState(patrolStatus);
    aiBot_.behaviorRootStatus = ToBehaviorDebugState(patrolStatus);
    return patrolStatus;
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotEvade(float deltaSeconds)
{
    if (aiBot_.desiredState != EAIBotState::Evade)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
    const glm::vec3 playerEyePos = GetEyePosition();
    const glm::vec3 toPlayer = playerEyePos - GetAIBotEyePosition();
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(playerEyePos.x - aiPos.x, playerEyePos.z - aiPos.z));
    aiBot_.state = EAIBotState::Evade;
    aiBot_.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : aiBot_.lookDir;

    const glm::vec3 strafeDir =
        NormalizeHorizontalOrZero(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toPlayerDir)) * aiBot_.strafeSign;
    const glm::vec3 evadeDir = NormalizeHorizontalOrZero(-toPlayerDir + strafeDir * 0.5f);

    if (navGrid_.IsBuilt())
    {
        const glm::vec3 evadeTarget = aiPos + evadeDir * 8.0f;
        if (aiBot_.pathFollower.NeedsRepath(evadeTarget, deltaSeconds, 0.3f, 1.5f))
        {
            auto path = navGrid_.FindPath(aiPos, evadeTarget, aiPos.y);
            if (path.empty())
            {
                path = navGrid_.FindPath(aiPos, aiPos - toPlayerDir * 8.0f, aiPos.y);
            }
            aiBot_.pathFollower.SetPath(std::move(path), evadeTarget);
        }
        glm::vec3 pathDir = aiBot_.pathFollower.GetMoveDirection(aiPos);
        aiBot_.moveDir = glm::length(pathDir) > 0.001f ? pathDir : evadeDir;
    }
    else
    {
        aiBot_.moveDir = evadeDir;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotAttack(float deltaSeconds)
{
    (void)deltaSeconds;

    if (aiBot_.desiredState != EAIBotState::Attack)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
    const glm::vec3 playerEyePos = GetEyePosition();
    const glm::vec3 toPlayer = playerEyePos - GetAIBotEyePosition();
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(playerEyePos.x - aiPos.x, playerEyePos.z - aiPos.z));
    aiBot_.state = EAIBotState::Attack;
    aiBot_.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : aiBot_.lookDir;

    glm::vec3 moveDir(0.0f);
    const glm::vec3 botForward(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    const float aimDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool inPreferredRange = distanceToPlayer <= aiPreferredCombatRangeMax_;

    if (distanceToPlayer > aiPreferredCombatRangeMax_)
    {
        moveDir = toPlayerDir;
    }
    else
    {
        moveDir = glm::vec3(0.0f);
    }

    aiBot_.moveDir = NormalizeHorizontalOrZero(moveDir);
    if (aiBot_.fireCooldownRemaining <= 0.0f && aimDot >= aiAimTolerance_)
    {
        const glm::vec3 shotDir = glm::normalize(playerEyePos - GetAIBotEyePosition());
        const glm::vec3 spawnCenter = GetAIBotEyePosition() + shotDir * projectileSpawnDistance_;
        SpawnProjectile("EnemyShotBox", spawnCenter, shotDir);
        aiBot_.fireCooldownRemaining = aiFireCooldown_;
        if (inPreferredRange)
        {
            aiBot_.strafeSign *= -1.0f;
        }
        aiBot_.patrolPauseRemaining = 0.0f;
        return EBehaviorTreeStatus::Success;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotChase(float deltaSeconds)
{
    if (aiBot_.desiredState != EAIBotState::Chase)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
    const glm::vec3 chaseTarget =
        aiBot_.targetVisible ? playerCharacter_.controller.GetPosition() : aiBot_.lastKnownTargetPosition;
    const glm::vec3 toTarget = chaseTarget - aiPos;
    const glm::vec3 chaseDir = NormalizeHorizontalOrZero(toTarget);
    const float distanceToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
    aiBot_.state = EAIBotState::Chase;
    aiBot_.lookDir = glm::length(chaseDir) > 0.001f ? chaseDir : aiBot_.lookDir;

    if (navGrid_.IsBuilt())
    {
        if (aiBot_.pathFollower.NeedsRepath(chaseTarget, deltaSeconds, 0.5f, 2.0f))
        {
            auto path = navGrid_.FindPath(aiPos, chaseTarget, aiPos.y);
            aiBot_.pathFollower.SetPath(std::move(path), chaseTarget);
        }
        glm::vec3 pathDir = aiBot_.pathFollower.GetMoveDirection(aiPos);
        aiBot_.moveDir = glm::length(pathDir) > 0.001f ? pathDir : chaseDir;
    }
    else
    {
        aiBot_.moveDir = chaseDir;
    }

    if (!aiBot_.targetVisible && distanceToTarget <= aiPatrolPointRadius_)
    {
        aiBot_.targetMemoryRemaining = 0.0f;
        aiBot_.patrolPauseRemaining = aiPatrolPauseTime_;
        return EBehaviorTreeStatus::Failure;
    }

    return EBehaviorTreeStatus::Running;
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotPatrol(float deltaSeconds)
{
    if (aiBot_.patrolPoints.empty())
    {
        aiBot_.state = EAIBotState::Disabled;
        return EBehaviorTreeStatus::Failure;
    }

    aiBot_.state = EAIBotState::Patrol;
    if (aiBot_.patrolPauseRemaining > 0.0f)
    {
        return EBehaviorTreeStatus::Running;
    }

    aiBot_.patrolIndex %= aiBot_.patrolPoints.size();
    const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();

    if (navGrid_.IsBuilt())
    {
        auto assignPatrolPath = [&](size_t reachablePatrolIndex, const glm::vec3& reachableTarget,
                                    std::vector<glm::vec3>&& reachablePath) -> void
        {
            aiBot_.patrolIndex = reachablePatrolIndex;
            aiBot_.pathFollower.SetPath(std::move(reachablePath), reachableTarget);
            aiBot_.patrolProgressAnchor = aiPos;
            aiBot_.patrolStuckTime = 0.0f;
            aiBot_.patrolAbandonedTarget = false;
        };

        auto trySelectNewPatrolTarget = [&](size_t startIndex, bool abandonedCurrentTarget) -> bool
        {
            size_t reachablePatrolIndex = startIndex;
            glm::vec3 reachableTarget(0.0f);
            std::vector<glm::vec3> reachablePath;
            if (!TryBuildReachablePatrolPath(aiPos, aiPos.y, startIndex,
                                             reachablePatrolIndex, reachableTarget, reachablePath))
            {
                aiBot_.pathFollower.Clear();
                aiBot_.moveDir = glm::vec3(0.0f);
                aiBot_.patrolStuckTime = 0.0f;
                aiBot_.patrolAbandonedTarget = abandonedCurrentTarget;
                aiBot_.patrolPauseRemaining = aiPatrolPauseTime_;
                aiBot_.patrolIndex = startIndex % aiBot_.patrolPoints.size();
                return false;
            }

            assignPatrolPath(reachablePatrolIndex, reachableTarget, std::move(reachablePath));
            aiBot_.patrolAbandonedTarget = abandonedCurrentTarget;
            return true;
        };

        if (!aiBot_.pathFollower.waypoints.empty() &&
            aiBot_.pathFollower.IsFinished(aiPos, aiPatrolPointRadius_))
        {
            aiBot_.pathFollower.Clear();
            aiBot_.patrolStuckTime = 0.0f;
            aiBot_.patrolAbandonedTarget = false;
            aiBot_.patrolIndex = (aiBot_.patrolIndex + 1) % aiBot_.patrolPoints.size();
            aiBot_.patrolPauseRemaining = aiPatrolPauseTime_;
            aiBot_.strafeSign *= -1.0f;
            return EBehaviorTreeStatus::Success;
        }

        const bool hasCommittedPatrolTarget = !aiBot_.pathFollower.waypoints.empty() && aiBot_.patrolReachableFound;
        const glm::vec3 requestedTarget = aiBot_.patrolPoints[aiBot_.patrolIndex];
        const glm::vec3 repathTarget = hasCommittedPatrolTarget ? aiBot_.patrolSelectedTarget : requestedTarget;
        if (aiBot_.pathFollower.NeedsRepath(repathTarget, deltaSeconds, 4.0f, 0.5f))
        {
            if (hasCommittedPatrolTarget)
            {
                std::vector<glm::vec3> refreshedPath = navGrid_.FindPath(aiPos, aiBot_.patrolSelectedTarget, aiPos.y);
                if (!refreshedPath.empty())
                {
                    assignPatrolPath(aiBot_.patrolIndex, aiBot_.patrolSelectedTarget, std::move(refreshedPath));
                }
                else if (!trySelectNewPatrolTarget((aiBot_.patrolIndex + 1) % aiBot_.patrolPoints.size(), true))
                {
                    return EBehaviorTreeStatus::Running;
                }
            }
            else if (!trySelectNewPatrolTarget(aiBot_.patrolIndex, false))
            {
                return EBehaviorTreeStatus::Running;
            }
        }

        glm::vec3 pathDir = aiBot_.pathFollower.GetMoveDirection(aiPos);
        if (glm::length(pathDir) > 0.001f)
        {
            aiBot_.moveDir = pathDir;
            aiBot_.lookDir = pathDir;

            const glm::vec3 anchorDelta = aiPos - aiBot_.patrolProgressAnchor;
            const float progressSinceAnchor = glm::length(glm::vec2(anchorDelta.x, anchorDelta.z));
            if (progressSinceAnchor >= aiPatrolProgressResetDistance_)
            {
                aiBot_.patrolProgressAnchor = aiPos;
                aiBot_.patrolStuckTime = 0.0f;
                aiBot_.patrolAbandonedTarget = false;
            }
            else
            {
                glm::vec3 horizontalVelocity = aiBot_.character.controller.GetLinearVelocity();
                horizontalVelocity.y = 0.0f;
                if (glm::length(horizontalVelocity) <= aiPatrolStuckSpeedThreshold_)
                {
                    aiBot_.patrolStuckTime += deltaSeconds;
                }
                else
                {
                    aiBot_.patrolStuckTime = 0.0f;
                }

                if (aiBot_.patrolStuckTime >= aiPatrolStuckTimeout_)
                {
                    const size_t nextPatrolIndex = (aiBot_.patrolIndex + 1) % aiBot_.patrolPoints.size();
                    aiBot_.pathFollower.Clear();
                    if (!trySelectNewPatrolTarget(nextPatrolIndex, true))
                    {
                        return EBehaviorTreeStatus::Running;
                    }

                    pathDir = aiBot_.pathFollower.GetMoveDirection(aiPos);
                    if (glm::length(pathDir) > 0.001f)
                    {
                        aiBot_.moveDir = pathDir;
                        aiBot_.lookDir = pathDir;
                    }
                    else
                    {
                        aiBot_.moveDir = glm::vec3(0.0f);
                    }
                }
            }
        }
        else
        {
            aiBot_.pathFollower.Clear();
            aiBot_.moveDir = glm::vec3(0.0f);
            aiBot_.patrolStuckTime = 0.0f;
        }
    }
    else
    {
        const glm::vec3 patrolTarget = aiBot_.patrolPoints[aiBot_.patrolIndex];
        const glm::vec3 toPatrol = patrolTarget - aiPos;
        const glm::vec3 patrolDir = NormalizeHorizontalOrZero(toPatrol);
        const float distanceToPatrol = glm::length(glm::vec2(toPatrol.x, toPatrol.z));
        if (distanceToPatrol <= aiPatrolPointRadius_)
        {
            aiBot_.patrolIndex = (aiBot_.patrolIndex + 1) % aiBot_.patrolPoints.size();
            aiBot_.patrolPauseRemaining = aiPatrolPauseTime_;
            aiBot_.strafeSign *= -1.0f;
            return EBehaviorTreeStatus::Success;
        }

        aiBot_.moveDir = patrolDir;
        aiBot_.lookDir = glm::length(patrolDir) > 0.001f ? patrolDir : aiBot_.lookDir;
    }

    return EBehaviorTreeStatus::Running;
}

void CharacterDemoGameInstance::UpdateAIBotNode()
{
    const glm::vec3 position = aiBot_.character.controller.GetPosition();
    aiBot_.character.SyncTransform(position, aiBot_.yaw);

    if (!aiBot_.character.actorRoot)
    {
        return;
    }
    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::TryInitAIBotCharacterModel()
{
    if (!aiBot_.character.TryResolveSkinnedModel(engine_->GetScene(), characterAppendRootName_, 0,
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
        aiBot_.character.RemoveVisualRoot(engine_->GetScene(), engine_->GetPhysicsEngine());
        aiBot_.visualNode.reset();
    }

    aiBot_.character.AttachSkinnedModel(engine_->GetPhysicsEngine());

    Runtime::SkinnedMeshComponent::FootPlacementIKSettings footPlacementSettings;
    footPlacementSettings.Enabled = footIKEnabled_;
    footPlacementSettings.Weight = aiBot_.character.controller.IsOnGround() ? 1.0f : 0.0f;
    footPlacementSettings.TraceUpDistance = 0.45f;
    footPlacementSettings.TraceDownDistance = 0.90f;
    footPlacementSettings.FootHeight = 0.025f;
    footPlacementSettings.MaxFootLift = 0.28f;
    footPlacementSettings.MaxFootDrop = 0.35f;
    footPlacementSettings.PelvisWeight = 0.75f;
    footPlacementSettings.PelvisMaxOffset = 0.22f;
    footPlacementSettings.DebugDraw = showFootIKDebug_;
    aiBot_.character.ConfigureFootPlacementIK(footPlacementSettings);

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
    if (!aiBot_.character.IsAnimationReady())
    {
        return;
    }

    NextGameplay::FCharacterAnimationUpdateInput input;
    input.velocity = aiBot_.character.controller.GetLinearVelocity();
    input.commandedMoveDir = aiBot_.character.control->GetMoveIntent();
    input.deltaSeconds = deltaSeconds;
    input.onGround = aiBot_.character.controller.IsOnGround();
    input.referenceForward = glm::vec3(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    input.referenceRight = glm::vec3(-input.referenceForward.z, 0.0f, input.referenceForward.x);
    input.desiredSpeed = aiBot_.character.control->GetDesiredSpeed();
    input.sprinting = aiBot_.character.control->GetSprinting();

    switch (aiBot_.state)
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

    aiBot_.character.UpdateAnimation(input, footIKEnabled_, showFootIKDebug_);
}

void CharacterDemoGameInstance::TryInitCharacterModel()
{
    if (!playerCharacter_.TryResolveSkinnedModel(engine_->GetScene(), characterAppendRootName_, 0))
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
    playerCharacter_.RemoveVisualRoot(engine_->GetScene(), engine_->GetPhysicsEngine());

    // Character collision should come only from the controller, not the imported skinned mesh hierarchy.
    // Otherwise the appended mannequin can leave kinematic mesh colliders around the origin / T-pose.
    playerCharacter_.AttachSkinnedModel(engine_->GetPhysicsEngine());

    // Apply current first-person visibility and start idle animation
    SetFirstPersonMode(firstPersonMode_);

    Runtime::SkinnedMeshComponent::FootPlacementIKSettings footPlacementSettings;
    footPlacementSettings.Enabled = footIKEnabled_;
    footPlacementSettings.Weight = playerCharacter_.controller.IsOnGround() ? 1.0f : 0.0f;
    footPlacementSettings.TraceUpDistance = 0.45f;
    footPlacementSettings.TraceDownDistance = 0.90f;
    footPlacementSettings.FootHeight = 0.025f;
    footPlacementSettings.MaxFootLift = 0.28f;
    footPlacementSettings.MaxFootDrop = 0.35f;
    footPlacementSettings.PelvisWeight = 0.75f;
    footPlacementSettings.PelvisMaxOffset = 0.22f;
    footPlacementSettings.DebugDraw = showFootIKDebug_;
    playerCharacter_.ConfigureFootPlacementIK(footPlacementSettings);

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

    playerCharacter_.UpdateAnimation(input, footIKEnabled_, showFootIKDebug_);
}

void CharacterDemoGameInstance::ResetCharacterState()
{
    aiBot_.character.Reset();
    aiBot_.visualNode.reset();
    aiBot_.patrolPoints.clear();
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.lookDir = glm::vec3(0.0f, 0.0f, 1.0f);
    aiBot_.lastKnownTargetPosition = glm::vec3(0.0f);
    aiBot_.yaw = 0.0f;
    aiBot_.fireCooldownRemaining = 0.0f;
    aiBot_.targetMemoryRemaining = 0.0f;
    aiBot_.patrolPauseRemaining = 0.0f;
    aiBot_.targetVisibleGraceRemaining = 0.0f;
    aiBot_.stateHoldRemaining = 0.0f;
    aiBot_.strafeSign = 1.0f;
    aiBot_.patrolIndex = 0;
    aiBot_.targetVisible = false;
    aiBot_.triggerJump = false;
    aiBot_.state = EAIBotState::Disabled;
    aiBot_.desiredState = EAIBotState::Disabled;
    aiBot_.patrolReachableFound = false;
    aiBot_.patrolUsedNearFallback = false;
    aiBot_.patrolRequestedIndex = 0;
    aiBot_.patrolSelectedIndex = 0;
    aiBot_.patrolCandidatesTested = 0;
    aiBot_.patrolWaypointCount = 0;
    aiBot_.patrolSelectionMs = 0.0f;
    aiBot_.patrolSelectedDistance = 0.0f;
    aiBot_.patrolStuckTime = 0.0f;
    aiBot_.patrolAbandonedTarget = false;
    aiBot_.patrolLastCommittedIndex = std::numeric_limits<size_t>::max();
    aiBot_.patrolSelectedTarget = glm::vec3(0.0f);
    aiBot_.patrolProgressAnchor = glm::vec3(0.0f);
    aiBot_.agentComponent.reset();

    playerCharacter_.Reset();
    characterYaw_ = yaw_;
}

void CharacterDemoGameInstance::SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
{
    NextGameplay::SetNodeVisibilityRecursive(node, visible);
}

void CharacterDemoGameInstance::SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
{
    NextGameplay::SetNodeRayCastVisibilityRecursive(node, visible);
}

void CharacterDemoGameInstance::DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node)
{
    NextGameplay::DisableNodePhysicsRecursive(node, engine_->GetPhysicsEngine());
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
        const float maxStep = characterTurnSpeed_ * deltaSeconds;

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

const char* CharacterDemoGameInstance::GetMovementModeName() const
{
    switch (movementMode_)
    {
    case ECharacterMovementMode::CameraAligned:
        return "CameraAligned";
    case ECharacterMovementMode::MoveAligned:
        return "MoveAligned";
    default:
        return "Unknown";
    }
}

const char* CharacterDemoGameInstance::GetBehaviorDebugStateName(EBehaviorDebugState state) const
{
    switch (state)
    {
    case EBehaviorDebugState::Inactive:
        return "Inactive";
    case EBehaviorDebugState::Failure:
        return "Failure";
    case EBehaviorDebugState::Success:
        return "Success";
    case EBehaviorDebugState::Running:
        return "Running";
    default:
        return "Unknown";
    }
}

CharacterDemoGameInstance::EBehaviorDebugState CharacterDemoGameInstance::ToBehaviorDebugState(
    EBehaviorTreeStatus status) const
{
    switch (status)
    {
    case EBehaviorTreeStatus::Failure:
        return EBehaviorDebugState::Failure;
    case EBehaviorTreeStatus::Success:
        return EBehaviorDebugState::Success;
    case EBehaviorTreeStatus::Running:
        return EBehaviorDebugState::Running;
    default:
        return EBehaviorDebugState::Inactive;
    }
}

void CharacterDemoGameInstance::DrawAIDebugMenu()
{
    if (!showAIDebugMenu_)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 280.0f, viewport->WorkPos.y + 14.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(262.0f, 0.0f), ImGuiCond_Always);

    if (ImGui::Begin("AI Debug Menu", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextUnformatted("F8 toggles this menu");
        ImGui::Separator();
        ImGui::Text("1 - Behavior Tree Overlay [%s]", showBehaviorTreeDebug_ ? "On" : "Off");
        ImGui::Text("2 - NavGrid Overlay [%s]", showNavGridDebug_ ? "On" : "Off");
        ImGui::TextDisabled("3-9 reserved for future AI debug toggles");
        ImGui::Text("0 - Close AI Debug Menu");
    }
    ImGui::End();
}

void CharacterDemoGameInstance::DrawAIBotBehaviorTreeUI() const
{
    if (!showBehaviorTreeDebug_)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 overlaySize(760.0f, 360.0f);
    const ImVec2 overlayPos(
        viewport->WorkPos.x + viewport->WorkSize.x - overlaySize.x - 18.0f,
        viewport->WorkPos.y + 112.0f);

    ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin(
        "##AIBehaviorTreeOverlay",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetWindowPos();
    const float rounding = 12.0f;

    auto getStateColors = [](EBehaviorDebugState state, bool emphasize) -> std::pair<ImU32, ImU32>
    {
        ImVec4 border(0.45f, 0.48f, 0.54f, emphasize ? 0.95f : 0.55f);
        ImVec4 fill(0.07f, 0.09f, 0.12f, emphasize ? 0.88f : 0.68f);
        switch (state)
        {
        case EBehaviorDebugState::Failure:
            border = ImVec4(0.88f, 0.36f, 0.32f, emphasize ? 1.0f : 0.78f);
            fill = ImVec4(0.26f, 0.08f, 0.08f, emphasize ? 0.92f : 0.72f);
            break;
        case EBehaviorDebugState::Success:
            border = ImVec4(0.30f, 0.82f, 0.45f, emphasize ? 1.0f : 0.80f);
            fill = ImVec4(0.08f, 0.20f, 0.12f, emphasize ? 0.92f : 0.72f);
            break;
        case EBehaviorDebugState::Running:
            border = ImVec4(1.00f, 0.78f, 0.22f, emphasize ? 1.0f : 0.86f);
            fill = ImVec4(0.26f, 0.20f, 0.06f, emphasize ? 0.94f : 0.76f);
            break;
        case EBehaviorDebugState::Inactive:
        default:
            break;
        }
        return {ImGui::GetColorU32(fill), ImGui::GetColorU32(border)};
    };

    auto drawNodeBox = [&](const ImVec2& center,
                           const ImVec2& size,
                           const char* typeLabel,
                           const char* title,
                           const char* subtitle,
                           EBehaviorDebugState state,
                           bool emphasize)
    {
        const auto [fillColor, borderColor] = getStateColors(state, emphasize);
        const ImVec2 min(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
        const ImVec2 max(center.x + size.x * 0.5f, center.y + size.y * 0.5f);
        const ImVec2 shadowOffset(0.0f, 10.0f);

        drawList->AddRectFilled(min + shadowOffset, max + shadowOffset, IM_COL32(0, 0, 0, 72), rounding + 2.0f);
        drawList->AddRectFilled(min, max, fillColor, rounding);
        drawList->AddRect(min, max, borderColor, rounding, 0, emphasize ? 3.0f : 2.0f);

        const ImVec2 typePos(min.x + 14.0f, min.y + 10.0f);
        drawList->AddText(typePos, IM_COL32(235, 238, 242, emphasize ? 245 : 190), typeLabel);

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        const ImVec2 titlePos(center.x - titleSize.x * 0.5f, min.y + 32.0f);
        drawList->AddText(titlePos, IM_COL32(248, 249, 250, 255), title);

        const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
        const ImVec2 subtitlePos(center.x - subtitleSize.x * 0.5f, min.y + 56.0f);
        drawList->AddText(subtitlePos, IM_COL32(186, 194, 204, 220), subtitle);

        const char* stateLabel = GetBehaviorDebugStateName(state);
        const ImVec2 stateSize = ImGui::CalcTextSize(stateLabel);
        const ImVec2 badgeMin(max.x - stateSize.x - 24.0f, min.y + 10.0f);
        const ImVec2 badgeMax(max.x - 12.0f, min.y + 28.0f);
        drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(255, 255, 255, emphasize ? 26 : 16), 9.0f);
        drawList->AddText(ImVec2(badgeMin.x + 8.0f, badgeMin.y + 2.0f), borderColor, stateLabel);

        return std::pair<ImVec2, ImVec2>(
            ImVec2(center.x, max.y),
            ImVec2(center.x, min.y));
    };

    auto drawConnection = [&](const ImVec2& from, const ImVec2& to, EBehaviorDebugState state, bool emphasize)
    {
        const auto [fillColor, borderColor] = getStateColors(state, emphasize);
        (void)fillColor;
        const ImVec2 controlA(from.x, from.y + 34.0f);
        const ImVec2 controlB(to.x, to.y - 34.0f);
        drawList->AddBezierCubic(from, controlA, controlB, to, borderColor, emphasize ? 3.0f : 2.0f);
    };

    drawList->AddText(ImVec2(base.x + 18.0f, base.y + 8.0f), IM_COL32(245, 246, 247, 255), "AI Behavior Tree");
    drawList->AddText(ImVec2(base.x + 18.0f, base.y + 28.0f), IM_COL32(180, 188, 198, 225),
                      "Runtime Overlay  |  Unreal-style debug view");

    std::string summary = "State: ";
    summary += aiEnabled_ ? GetAIBotStateName() : "Disabled";
    if (aiBot_.character.controller.IsValid())
    {
        const glm::vec3 playerPos = playerCharacter_.controller.GetPosition();
        const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
        const float distance = glm::length(glm::vec2(playerPos.x - aiPos.x, playerPos.z - aiPos.z));
        summary += fmt::format("  |  Dist {:.1f}  |  Visible {}  |  LOS {}",
                               distance,
                               aiBot_.targetVisible ? "Yes" : "No",
                               HasLineOfSightToPlayer() ? "Yes" : "No");
    }
    drawList->AddText(ImVec2(base.x + 18.0f, base.y + 52.0f), IM_COL32(215, 221, 228, 230), summary.c_str());

    if (navGrid_.IsBuilt())
    {
        std::string patrolDebug = fmt::format(
            "Patrol Reachable: {} | Req {} -> Sel {} | Tested {} | Waypoints {} | Dist {:.1f} | NearFallback {} | Abandoned {} | Stuck {:.2f}s | {:.2f} ms",
            aiBot_.patrolReachableFound ? "Yes" : "No",
            aiBot_.patrolRequestedIndex,
            aiBot_.patrolSelectedIndex,
            aiBot_.patrolCandidatesTested,
            aiBot_.patrolWaypointCount,
            aiBot_.patrolSelectedDistance,
            aiBot_.patrolUsedNearFallback ? "Yes" : "No",
            aiBot_.patrolAbandonedTarget ? "Yes" : "No",
            aiBot_.patrolStuckTime,
            aiBot_.patrolSelectionMs);
        drawList->AddText(ImVec2(base.x + 18.0f, base.y + 72.0f), IM_COL32(170, 210, 255, 230), patrolDebug.c_str());
    }

    const ImVec2 rootCenter(base.x + overlaySize.x * 0.50f, base.y + 102.0f);
    const ImVec2 selectorCenter(base.x + overlaySize.x * 0.50f, base.y + 186.0f);
    const ImVec2 leafRowY = ImVec2(0.0f, base.y + 292.0f);
    const ImVec2 leafSize(150.0f, 90.0f);
    const ImVec2 topSize(180.0f, 76.0f);
    const ImVec2 selectorSize(220.0f, 86.0f);

    const auto rootSockets = drawNodeBox(rootCenter, topSize, "ROOT", "BT Root", "CharacterDemo AI", aiBot_.behaviorRootStatus,
                                         aiBot_.behaviorRootStatus == EBehaviorDebugState::Running);
    const auto selectorSockets =
        drawNodeBox(selectorCenter, selectorSize, "SELECTOR", "Combat Root", "Evade -> Attack -> Chase -> Patrol",
                    aiBot_.behaviorRootStatus, aiBot_.behaviorRootStatus == EBehaviorDebugState::Running);

    drawConnection(rootSockets.first, selectorSockets.second, aiBot_.behaviorRootStatus,
                   aiBot_.behaviorRootStatus == EBehaviorDebugState::Running);

    struct FLeafDebugNode
    {
        const char* title;
        const char* subtitle;
        EBehaviorDebugState state;
        float x;
    };

    const std::array<FLeafDebugNode, 4> leafNodes{{
        {"Evade", "Too close, break contact", aiBot_.behaviorEvadeStatus, 130.0f},
        {"Attack", "Hold lane and fire", aiBot_.behaviorAttackStatus, 305.0f},
        {"Chase", "Close distance to target", aiBot_.behaviorChaseStatus, 480.0f},
        {"Patrol", "Fallback route sweep", aiBot_.behaviorPatrolStatus, 655.0f},
    }};

    for (const auto& leaf : leafNodes)
    {
        const ImVec2 center(base.x + leaf.x, leafRowY.y);
        const bool emphasize = leaf.state == EBehaviorDebugState::Running || leaf.state == EBehaviorDebugState::Success;
        const auto sockets = drawNodeBox(center, leafSize, "TASK", leaf.title, leaf.subtitle, leaf.state, emphasize);
        drawConnection(selectorSockets.first, sockets.second, leaf.state, emphasize);
    }

    ImGui::End();
}

const char* CharacterDemoGameInstance::GetAIBotStateName() const
{
    switch (aiBot_.state)
    {
    case EAIBotState::Disabled:
        return "Disabled";
    case EAIBotState::Patrol:
        return "Patrol";
    case EAIBotState::Chase:
        return "Chase";
    case EAIBotState::Evade:
        return "Evade";
    case EAIBotState::Attack:
        return "Attack";
    default:
        return "Unknown";
    }
}

const char* CharacterDemoGameInstance::GetAnimStateName() const
{
    return playerCharacter_.animation
        ? NextGameplay::GetCharacterAnimStateName(playerCharacter_.animation->GetAnimState())
        : "Unknown";
}

void CharacterDemoGameInstance::DrawNavGridDebugOverlay() const
{
    if (!navGrid_.IsBuilt())
    {
        return;
    }

    Assets::Camera cam = engine_->GetScene().GetRenderCamera();
    OverrideRenderCamera(cam);

    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
    {
        return;
    }

    const float aspect = viewportSize.x / viewportSize.y;
    const float fov = cam.FieldOfView > 1.0f ? cam.FieldOfView : 60.0f;
    const glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, 2000.0f);
    const glm::mat4 viewProjection = projection * cam.ModelView;

    auto project = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool
    {
        const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        {
            return false;
        }
        screenPos.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        screenPos.y = viewportPos.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y;
        return true;
    };

    auto* drawList = ImGui::GetForegroundDrawList();
    const float cellSize = navGrid_.GetCellSize();

    size_t reachableCount = 0;
    size_t disconnectedCount = 0;

    // Draw navgrid cells near the AI bot. Use the current connected component rather than a
    // single reference height, otherwise different floors collapse into the same green layer.
    if (aiBot_.character.controller.IsValid())
    {
        const glm::vec3 aiPos = aiBot_.character.controller.GetPosition();
        constexpr float drawRadius = 18.0f;
        const int cellRadius = static_cast<int>(drawRadius / cellSize);
        const auto reachableMask = navGrid_.BuildReachabilityMask(aiPos, aiPos.y);

        const glm::vec3 gridMin = navGrid_.GetWorldMin();
        const glm::ivec2 aiCell = {
            static_cast<int>(std::floor((aiPos.x - gridMin.x) / cellSize)),
            static_cast<int>(std::floor((aiPos.z - gridMin.z) / cellSize))
        };

        const ImU32 reachableFill = IM_COL32(60, 210, 110, 52);
        const ImU32 reachableOutline = IM_COL32(60, 220, 120, 150);
        const ImU32 disconnectedFill = IM_COL32(255, 180, 40, 44);
        const ImU32 disconnectedOutline = IM_COL32(255, 190, 40, 132);
        const ImU32 aiCellColor = IM_COL32(80, 190, 255, 230);
        constexpr float liftY = 0.05f;

        for (int dz = -cellRadius; dz <= cellRadius; ++dz)
        {
            for (int dx = -cellRadius; dx <= cellRadius; ++dx)
            {
                if ((dx * dx + dz * dz) > (cellRadius * cellRadius))
                {
                    continue;
                }

                const int gx = aiCell.x + dx;
                const int gz = aiCell.y + dz;
                if (gx < 0 || gx >= navGrid_.GetWidth() || gz < 0 || gz >= navGrid_.GetHeight())
                {
                    continue;
                }

                const glm::vec3 cellWorld = navGrid_.GetCellWorldPosition(gx, gz);
                const float halfExtent = cellSize * 0.46f;
                std::array<ImVec2, 4> projectedCorners {};
                const std::array<glm::vec2, 4> cornerOffsets = {
                    glm::vec2(-halfExtent, -halfExtent),
                    glm::vec2(halfExtent, -halfExtent),
                    glm::vec2(halfExtent, halfExtent),
                    glm::vec2(-halfExtent, halfExtent)
                };

                bool visible = true;
                for (size_t cornerIndex = 0; cornerIndex < cornerOffsets.size(); ++cornerIndex)
                {
                    const glm::vec2 offset = cornerOffsets[cornerIndex];
                    const glm::vec3 cornerWorld(cellWorld.x + offset.x, cellWorld.y + liftY, cellWorld.z + offset.y);
                    if (!project(cornerWorld, projectedCorners[cornerIndex]))
                    {
                        visible = false;
                        break;
                    }
                }

                if (!visible || !navGrid_.IsCellWalkable(gx, gz))
                {
                    continue;
                }

                const size_t cellIndex = static_cast<size_t>(gz * navGrid_.GetWidth() + gx);
                const bool isConnected = cellIndex < reachableMask.size() && reachableMask[cellIndex] != 0;
                if (isConnected)
                {
                    ++reachableCount;
                }
                else
                {
                    ++disconnectedCount;
                }

                const ImU32 fillColor = isConnected ? reachableFill : disconnectedFill;
                const ImU32 outlineColor = isConnected ? reachableOutline : disconnectedOutline;
                drawList->AddConvexPolyFilled(projectedCorners.data(), static_cast<int>(projectedCorners.size()), fillColor);
                drawList->AddPolyline(projectedCorners.data(), static_cast<int>(projectedCorners.size()), outlineColor, true, 1.0f);

                if (gx == aiCell.x && gz == aiCell.y)
                {
                    ImVec2 screenCenter;
                    if (project(glm::vec3(cellWorld.x, cellWorld.y + 0.12f, cellWorld.z), screenCenter))
                    {
                        drawList->AddCircle(screenCenter, 5.0f, aiCellColor, 10, 2.0f);
                    }
                }
            }
        }
    }

    // Draw current AI path
    const auto& waypoints = aiBot_.pathFollower.waypoints;
    if (waypoints.size() >= 2)
    {
        const ImU32 pathColor = IM_COL32(255, 200, 0, 200);
        const ImU32 waypointColor = IM_COL32(255, 255, 0, 220);
        const ImU32 currentWpColor = IM_COL32(0, 255, 128, 255);
        constexpr float liftY = 0.15f;

        for (size_t i = 0; i + 1 < waypoints.size(); ++i)
        {
            ImVec2 sa, sb;
            glm::vec3 a = waypoints[i];
            a.y += liftY;
            glm::vec3 b = waypoints[i + 1];
            b.y += liftY;
            if (project(a, sa) && project(b, sb))
            {
                drawList->AddLine(sa, sb, pathColor, 2.5f);
            }
        }

        for (size_t i = 0; i < waypoints.size(); ++i)
        {
            ImVec2 sp;
            glm::vec3 wp = waypoints[i];
            wp.y += liftY;
            if (project(wp, sp))
            {
                const bool isCurrent = (i == aiBot_.pathFollower.currentIndex);
                drawList->AddCircleFilled(sp, isCurrent ? 5.0f : 3.0f, isCurrent ? currentWpColor : waypointColor, 8);
            }
        }
    }

    // Draw info text
    if (aiBot_.character.controller.IsValid())
    {
        ImVec2 textPos(viewportPos.x + 10.0f, viewportPos.y + viewportSize.y - 60.0f);
        char buf[160];
        snprintf(buf, sizeof(buf), "NavGrid: %dx%d | Connected: %zu | OtherWalkable: %zu | Path: %zu | WP: %zu",
                 navGrid_.GetWidth(), navGrid_.GetHeight(),
                 reachableCount, disconnectedCount, waypoints.size(), aiBot_.pathFollower.currentIndex);
        drawList->AddText(textPos, IM_COL32(255, 255, 200, 220), buf);
    }
}
