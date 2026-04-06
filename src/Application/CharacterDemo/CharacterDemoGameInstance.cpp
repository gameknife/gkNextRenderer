#include "CharacterDemoGameInstance.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <spdlog/spdlog.h>

#include "Assets/Loaders/FProcModel.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Runtime/Utilities/GraphicsDebugPanel.hpp"
#include "Runtime/Utilities/PhysicsDebugOverlay.hpp"
#include "Vulkan/WindowSurface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<CharacterDemoGameInstance>(config, options, engine);
}

namespace
{
    glm::vec3 NormalizeHorizontalOrZero(const glm::vec3& value)
    {
        glm::vec3 horizontal(value.x, 0.0f, value.z);
        const float length = glm::length(horizontal);
        if (length <= 0.001f)
        {
            return glm::vec3(0.0f);
        }
        return horizontal / length;
    }

    float AdvanceYawToward(float currentYaw, const glm::vec3& desiredDirection, float turnSpeed, float deltaSeconds)
    {
        const glm::vec3 horizontalDir = NormalizeHorizontalOrZero(desiredDirection);
        if (glm::length(horizontalDir) <= 0.001f)
        {
            return currentYaw;
        }

        const float targetYaw = std::atan2(horizontalDir.x, horizontalDir.z);
        const float yawDelta = std::remainder(targetYaw - currentYaw, glm::two_pi<float>());
        const float maxStep = turnSpeed * deltaSeconds;
        if (std::abs(yawDelta) <= maxStep)
        {
            return targetYaw;
        }
        return currentYaw + glm::sign(yawDelta) * maxStep;
    }

    bool HasSkinnedMeshInHierarchy(const std::shared_ptr<Assets::Node>& node)
    {
        if (!node)
        {
            return false;
        }

        if (node->GetComponent<Runtime::SkinnedMeshComponent>())
        {
            return true;
        }

        for (const auto& child : node->Children())
        {
            if (HasSkinnedMeshInHierarchy(child))
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<Assets::Node> FindAppendedCharacterRoot(const Assets::Scene& scene,
                                                            const std::string& baseName,
                                                            size_t ordinal,
                                                            const std::shared_ptr<Assets::Node>& exclude = nullptr)
    {
        std::vector<std::shared_ptr<Assets::Node>> candidates;
        for (const auto& node : scene.Nodes())
        {
            if (!node || node->GetParent() != nullptr)
            {
                continue;
            }

            const std::string& nodeName = node->GetName();
            if (nodeName != baseName && !nodeName.starts_with(baseName + "_"))
            {
                continue;
            }

            if (exclude && node->GetInstanceId() == exclude->GetInstanceId())
            {
                continue;
            }

            if (!HasSkinnedMeshInHierarchy(node))
            {
                continue;
            }

            candidates.push_back(node);
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const std::shared_ptr<Assets::Node>& lhs, const std::shared_ptr<Assets::Node>& rhs)
                  {
                      return lhs->GetInstanceId() < rhs->GetInstanceId();
                  });

        if (ordinal >= candidates.size())
        {
            return nullptr;
        }

        return candidates[ordinal];
    }
}

CharacterDemoGameInstance::CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
    config.Height = 720;
    config.Width = 1280;
}

void CharacterDemoGameInstance::OnInit()
{
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
    if (!characterController_.IsValid())
    {
        return;
    }

    // Try to find and init the skinned character model once it's loaded
    if (characterLoadRequested_ && !characterModelLoaded_)
    {
        TryInitCharacterModel();
    }
    if (aiBot_.characterLoadRequested && !aiBot_.characterModelLoaded)
    {
        TryInitAIBotCharacterModel();
    }

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

    characterController_.Update(moveDir, speed, keyJump_, static_cast<float>(deltaSeconds));
    keyJump_ = false; // consume jump

    const glm::vec3 currentVelocity = characterController_.GetLinearVelocity();

    UpdateCharacterFacingYaw(moveDir, currentVelocity, static_cast<float>(deltaSeconds));
    UpdateCharacterNode();
    UpdateAnimationState(static_cast<float>(deltaSeconds));
    UpdateCharacterAnimationPostProcess();
    UpdateAIBot(static_cast<float>(deltaSeconds));
}

void CharacterDemoGameInstance::OnDestroy()
{
    characterController_.Destroy();
    aiBot_.controller.Destroy();
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

    characterController_.Create(engine_->GetPhysicsEngine(), settings);
    wasOnGroundLastFrame_ = true;
    jumpStartHoldTimeRemaining_ = 0.0f;
    jumpLandHoldTimeRemaining_ = 0.0f;

    // Extract yaw from camera
    glm::vec3 camForward = -glm::vec3(invModelView[2]);
    yaw_ = std::atan2(camForward.x, camForward.z);
    pitch_ = std::asin(glm::clamp(camForward.y, -1.0f, 1.0f));
    characterYaw_ = yaw_;

    // Create the character visual node
    uint32_t instanceId = engine_->GetScene().GenerateInstanceId();
    characterNode_ = Assets::Node::CreateNode("CharacterBody",
        settings.initialPosition, glm::quat(1, 0, 0, 0), glm::vec3(1), instanceId);

    auto renderComp = std::make_shared<Runtime::RenderComponent>();
    renderComp->SetModelId(capsuleModelId_);
    renderComp->SetMaterial({characterMatId_});
    renderComp->SetVisible(true);
    characterNode_->AddComponent(renderComp);

    // Prevent Scene::AddNode from auto-creating a static mesh body for the visual-only character node.
    auto physicsComp = std::make_shared<Runtime::PhysicsComponent>();
    physicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
    characterNode_->AddComponent(physicsComp);

    engine_->GetScene().AddNode(characterNode_);
    InitAIBot();
    SetFirstPersonMode(firstPersonMode_);
    engine_->GetScene().MarkDirty();

    engine_->GetScene().GetEnvSettings().SkyIdx = 2;
    
    // Load the skinned character model asynchronously
    engine_->RequestLoadSceneAdd("assets/models/characters/Mannequin_Medium.glb");
    characterLoadRequested_ = true;
    engine_->RequestLoadSceneAdd("assets/models/characters/Mannequin_Medium.glb");
    aiBot_.characterLoadRequested = true;

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

bool CharacterDemoGameInstance::OnRenderUI()
{
    if (!characterController_.IsValid())
    {
        return false;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(280, 0));
    ImGui::Begin("Character Demo", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    glm::vec3 pos = characterController_.GetPosition();
    glm::vec3 vel = characterController_.GetLinearVelocity();
    bool onGround = characterController_.IsOnGround();

    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("Velocity: %.1f, %.1f, %.1f", vel.x, vel.y, vel.z);
    ImGui::Text("On Ground: %s", onGround ? "Yes" : "No");
    ImGui::Text("View: %s", firstPersonMode_ ? "FPS" : "TPS");
    ImGui::Text("Move Mode: %s", GetMovementModeName());
    ImGui::Text("Graphics Debug: %s", showGraphicsDebug_ ? "On" : "Off");
    ImGui::Text("Physics Debug: %s", showPhysicsDebug_ ? "On" : "Off");
    ImGui::Text("Foot IK: %s", footIKEnabled_ ? "On" : "Off");
    ImGui::Text("Foot IK Debug: %s", showFootIKDebug_ ? "On" : "Off");
    ImGui::Text("AI: %s", aiEnabled_ ? GetAIBotStateName() : "Disabled");
    if (characterModelLoaded_)
    {
        ImGui::Text("Anim State: %s", GetAnimStateName());
        if (primarySkinnedMeshComp_)
        {
            ImGui::Text("Playing: %s", primarySkinnedMeshComp_->GetCurrentAnimationName().c_str());
        }
    }
    else if (characterLoadRequested_)
    {
        ImGui::Text("Character: Loading...");
    }
    if (aiBot_.controller.IsValid())
    {
        const glm::vec3 aiPos = aiBot_.controller.GetPosition();
        const float botDistance = glm::distance(aiPos, pos);
        ImGui::Text("AI Pos: %.1f, %.1f, %.1f", aiPos.x, aiPos.y, aiPos.z);
        ImGui::Text("AI Dist: %.1f | Visible: %s | LOS: %s", botDistance, aiBot_.targetVisible ? "Yes" : "No",
                    HasLineOfSightToPlayer() ? "Yes" : "No");
        if (aiBot_.characterModelLoaded && aiBot_.primarySkinnedMeshComp)
        {
            ImGui::Text("AI Clip: %s", aiBot_.primarySkinnedMeshComp->GetCurrentAnimationName().c_str());
        }
    }
    ImGui::Separator();
    ImGui::Text("WASD - Move | Shift - Run");
    ImGui::Text("Space - Jump | Mouse - Look");
    ImGui::Text("V - Toggle FPS/TPS | Tab - Move Mode");
    ImGui::Text("LMB - Shoot | F1 - Physics | F2 - Graphics | F3 - Foot IK");
    ImGui::Text("1-8 - View Modes | F9 - IK Debug");
    ImGui::Text("ESC - Release Mouse");

    ImGui::SliderFloat("Walk Speed", &walkSpeed_, 1.0f, 10.0f);
    ImGui::SliderFloat("Run Speed", &runSpeed_, 5.0f, 20.0f);
    ImGui::SliderFloat("Camera Dist", &cameraDistance_, 1.0f, 15.0f);
    ImGui::Checkbox("Enable AI", &aiEnabled_);
    ImGui::SliderFloat("AI Sight", &aiSightRange_, 8.0f, 60.0f);
    ImGui::SliderFloat("AI Fire Range", &aiFireRange_, 4.0f, 40.0f);
    ImGui::SliderFloat("AI Fire Cooldown", &aiFireCooldown_, 0.2f, 4.0f);

    ImGui::End();

    DrawAIBotBehaviorTreeUI();
    Runtime::GraphicsDebugPanel::DrawPanel(*engine_, showGraphicsDebug_, 0.0f);

    if (showPhysicsDebug_)
    {
        Assets::Camera debugCamera = engine_->GetScene().GetRenderCamera();
        OverrideRenderCamera(debugCamera);
        Runtime::DrawPhysicsDebugOverlay(engine_->GetScene(), debugCamera);
        Runtime::DrawCharacterControllerDebugOverlay(characterController_, debugCamera);
        if (aiBot_.controller.IsValid())
        {
            Runtime::DrawCharacterControllerDebugOverlay(aiBot_.controller, debugCamera);
        }
    }

    return true;
}

bool CharacterDemoGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    if (!characterController_.IsValid())
    {
        return false;
    }

    glm::vec3 charPos = characterController_.GetPosition();
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
    case SDLK_F2:
        if (pressed)
        {
            showGraphicsDebug_ = !showGraphicsDebug_;
        }
        return true;
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
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
    case SDLK_7:
    case SDLK_8:
    case SDLK_KP_1:
    case SDLK_KP_2:
    case SDLK_KP_3:
    case SDLK_KP_4:
    case SDLK_KP_5:
    case SDLK_KP_6:
    case SDLK_KP_7:
    case SDLK_KP_8:
        if (Runtime::GraphicsDebugPanel::TryHandleViewModeShortcut(key, pressed, showGraphicsDebug_,
                                                                   engine_->GetShowFlags()))
        {
            return true;
        }
        return false;
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
    return characterController_.GetPosition() + glm::vec3(0.0f, firstPersonEyeHeight_, 0.0f);
}

glm::vec3 CharacterDemoGameInstance::GetAIBotEyePosition() const
{
    return aiBot_.controller.GetPosition() + glm::vec3(0.0f, aiEyeHeight_, 0.0f);
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

    if (characterNode_)
    {
        if (auto renderComp = characterNode_->GetComponent<Runtime::RenderComponent>())
        {
            renderComp->SetVisible(!firstPersonMode_ && !characterModelLoaded_);
        }
    }

    SetNodeVisibilityRecursive(skinnedCharacterRoot_, !firstPersonMode_);

    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::FireProjectile()
{
    if (!characterController_.IsValid())
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
    glm::vec3 pos = characterController_.GetPosition();
    glm::quat rotation = glm::angleAxis(GetCharacterYaw(), glm::vec3(0.0f, 1.0f, 0.0f));

    // Update box placeholder (visible until skinned model loads)
    if (characterNode_)
    {
        characterNode_->SetTranslation(pos);
        characterNode_->SetRotation(rotation);
        characterNode_->RecalcTransform(true);
    }

    // Update the entire appended character hierarchy instead of only one skinned mesh node.
    if (skinnedCharacterRoot_)
    {
        skinnedCharacterRoot_->SetTranslation(pos);
        skinnedCharacterRoot_->SetRotation(rotation);
        skinnedCharacterRoot_->RecalcTransform(true);
    }

    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::InitAIBot()
{
    aiBot_.controller.Destroy();
    aiBot_.visualNode.reset();
    aiBot_.skinnedRoot.reset();
    aiBot_.primarySkinnedMeshComp = nullptr;
    aiBot_.skinnedMeshComps.clear();
    aiBot_.patrolPoints.clear();
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.lookDir = glm::vec3(0.0f, 0.0f, 1.0f);
    aiBot_.lastKnownTargetPosition = characterController_.GetPosition();
    aiBot_.fireCooldownRemaining = 0.0f;
    aiBot_.targetMemoryRemaining = 0.0f;
    aiBot_.patrolPauseRemaining = 0.0f;
    aiBot_.strafeSign = 1.0f;
    aiBot_.patrolIndex = 0;
    aiBot_.targetVisible = false;
    aiBot_.triggerJump = false;
    aiBot_.state = aiEnabled_ ? EAIBotState::Patrol : EAIBotState::Disabled;
    aiBot_.appendRootName = characterAppendRootName_ + "_1";
    aiBot_.animState = ECharacterAnimState::Idle;
    aiBot_.characterModelLoaded = false;
    aiBot_.characterLoadRequested = false;
    aiBot_.wasOnGroundLastFrame = true;
    aiBot_.jumpStartHoldTimeRemaining = 0.0f;
    aiBot_.jumpLandHoldTimeRemaining = 0.0f;

    CollectAIBotPatrolPoints();

    glm::vec3 aiSpawn = characterController_.GetPosition() + glm::vec3(6.0f, 0.0f, 8.0f);
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
    aiBot_.controller.Create(engine_->GetPhysicsEngine(), settings);

    const glm::vec3 toPlayer = characterController_.GetPosition() - aiSpawn;
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

    const uint32_t instanceId = engine_->GetScene().GenerateInstanceId();
    aiBot_.visualNode = Assets::Node::CreateNode(
        "EnemyBot",
        aiSpawn,
        glm::quat(1, 0, 0, 0),
        glm::vec3(1.0f),
        instanceId);

    auto renderComp = std::make_shared<Runtime::RenderComponent>();
    renderComp->SetModelId(capsuleModelId_);
    renderComp->SetMaterial({aiCharacterMatId_});
    renderComp->SetVisible(true);
    renderComp->SetRayCastVisible(false);
    aiBot_.visualNode->AddComponent(renderComp);

    auto physicsComp = std::make_shared<Runtime::PhysicsComponent>();
    physicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
    aiBot_.visualNode->AddComponent(physicsComp);

    engine_->GetScene().AddNode(aiBot_.visualNode);
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
    if (!aiBot_.controller.IsValid() || !characterController_.IsValid())
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

void CharacterDemoGameInstance::UpdateAIBot(float deltaSeconds)
{
    if (!aiBot_.controller.IsValid())
    {
        aiBot_.behaviorRootStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorAttackStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorChaseStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        return;
    }

    aiBot_.fireCooldownRemaining = std::max(0.0f, aiBot_.fireCooldownRemaining - deltaSeconds);
    aiBot_.targetMemoryRemaining = std::max(0.0f, aiBot_.targetMemoryRemaining - deltaSeconds);
    aiBot_.patrolPauseRemaining = std::max(0.0f, aiBot_.patrolPauseRemaining - deltaSeconds);
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.triggerJump = false;

    const glm::vec3 aiPos = aiBot_.controller.GetPosition();
    const glm::vec3 playerPos = characterController_.GetPosition();
    const glm::vec3 toPlayer = playerPos - aiPos;
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
    const glm::vec3 botForward(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    const float fovDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool closeThreat = distanceToPlayer <= aiPreferredCombatRangeMin_;
    const bool hasLineOfSight = closeThreat || HasLineOfSightToPlayer();
    const float sightRange = aiBot_.targetVisible ? aiLoseSightRange_ : aiSightRange_;
    aiBot_.targetVisible =
        aiEnabled_ &&
        distanceToPlayer <= sightRange &&
        std::abs(toPlayer.y) <= 4.0f &&
        hasLineOfSight &&
        (closeThreat || fovDot >= std::cos(glm::radians(75.0f)));

    if (aiBot_.targetVisible)
    {
        aiBot_.lastKnownTargetPosition = playerPos;
        aiBot_.targetMemoryRemaining = aiMemoryTime_;
    }

    if (!aiEnabled_)
    {
        aiBot_.state = EAIBotState::Disabled;
        aiBot_.behaviorRootStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorEvadeStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorAttackStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorChaseStatus = EBehaviorDebugState::Inactive;
        aiBot_.behaviorPatrolStatus = EBehaviorDebugState::Inactive;
        aiBot_.controller.Update(glm::vec3(0.0f), 0.0f, false, deltaSeconds);
        UpdateAIBotAnimationState(deltaSeconds);
        UpdateAIBotNode();
        return;
    }

    RunAIBotBehaviorTree(deltaSeconds);

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

    aiBot_.controller.Update(aiBot_.moveDir, speed, aiBot_.triggerJump, deltaSeconds);

    glm::vec3 facingDirection = aiBot_.lookDir;
    if (glm::length(aiBot_.controller.GetLinearVelocity()) > 0.2f)
    {
        facingDirection = aiBot_.controller.GetLinearVelocity();
    }
    aiBot_.yaw = AdvanceYawToward(aiBot_.yaw, facingDirection, aiTurnSpeed_, deltaSeconds);

    UpdateAIBotAnimationState(deltaSeconds);
    UpdateAIBotNode();
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
    (void)deltaSeconds;

    if (!aiBot_.targetVisible)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiBot_.controller.GetPosition();
    const glm::vec3 playerEyePos = GetEyePosition();
    const glm::vec3 toPlayer = playerEyePos - GetAIBotEyePosition();
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(playerEyePos.x - aiPos.x, playerEyePos.z - aiPos.z));
    const float evadeThreshold =
        aiPreferredCombatRangeMin_ +
        (aiBot_.state == EAIBotState::Evade ? aiCombatRangeHysteresis_ : 0.0f);
    if (distanceToPlayer >= evadeThreshold)
    {
        return EBehaviorTreeStatus::Failure;
    }

    aiBot_.state = EAIBotState::Evade;
    aiBot_.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : aiBot_.lookDir;

    const glm::vec3 strafeDir =
        NormalizeHorizontalOrZero(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toPlayerDir)) * aiBot_.strafeSign;
    aiBot_.moveDir = NormalizeHorizontalOrZero(-toPlayerDir + strafeDir * 0.5f);
    return EBehaviorTreeStatus::Running;
}

CharacterDemoGameInstance::EBehaviorTreeStatus CharacterDemoGameInstance::RunAIBotAttack(float deltaSeconds)
{
    (void)deltaSeconds;

    if (!aiBot_.targetVisible)
    {
        return EBehaviorTreeStatus::Failure;
    }

    const glm::vec3 aiPos = aiBot_.controller.GetPosition();
    const glm::vec3 playerEyePos = GetEyePosition();
    const glm::vec3 toPlayer = playerEyePos - GetAIBotEyePosition();
    const glm::vec3 toPlayerDir = NormalizeHorizontalOrZero(toPlayer);
    const float distanceToPlayer = glm::length(glm::vec2(playerEyePos.x - aiPos.x, playerEyePos.z - aiPos.z));
    const float attackMaxRange =
        std::min(aiFireRange_,
                 aiPreferredCombatRangeMax_ +
                     (aiBot_.state == EAIBotState::Attack ? aiCombatRangeHysteresis_ : 0.0f));
    if (distanceToPlayer < aiPreferredCombatRangeMin_ || distanceToPlayer > attackMaxRange)
    {
        return EBehaviorTreeStatus::Failure;
    }

    aiBot_.state = EAIBotState::Attack;
    aiBot_.lookDir = glm::length(toPlayerDir) > 0.001f ? toPlayerDir : aiBot_.lookDir;

    glm::vec3 moveDir(0.0f);
    const glm::vec3 strafeDir = NormalizeHorizontalOrZero(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toPlayerDir)) * aiBot_.strafeSign;
    const glm::vec3 botForward(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    const float aimDot = glm::length(toPlayerDir) > 0.001f ? glm::dot(botForward, toPlayerDir) : 1.0f;
    const bool inPreferredRange = distanceToPlayer <= aiPreferredCombatRangeMax_;

    if (distanceToPlayer > aiPreferredCombatRangeMax_)
    {
        moveDir = toPlayerDir;
    }
    else if (aimDot < aiAimTolerance_ - 0.08f)
    {
        moveDir = strafeDir * 0.35f;
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
    (void)deltaSeconds;

    if (!aiBot_.targetVisible && aiBot_.targetMemoryRemaining <= 0.0f)
    {
        return EBehaviorTreeStatus::Failure;
    }

    aiBot_.state = EAIBotState::Chase;

    const glm::vec3 chaseTarget = aiBot_.targetVisible ? characterController_.GetPosition() : aiBot_.lastKnownTargetPosition;
    const glm::vec3 toTarget = chaseTarget - aiBot_.controller.GetPosition();
    const glm::vec3 chaseDir = NormalizeHorizontalOrZero(toTarget);
    const float distanceToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
    if (aiBot_.targetVisible)
    {
        const float chaseExitRange =
            std::max(aiPreferredCombatRangeMin_,
                     aiPreferredCombatRangeMax_ -
                         (aiBot_.state == EAIBotState::Chase ? aiCombatRangeHysteresis_ : 0.0f));
        if (distanceToTarget <= chaseExitRange)
        {
            return EBehaviorTreeStatus::Failure;
        }
    }

    aiBot_.lookDir = glm::length(chaseDir) > 0.001f ? chaseDir : aiBot_.lookDir;
    aiBot_.moveDir = chaseDir;
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
    (void)deltaSeconds;

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
    const glm::vec3 patrolTarget = aiBot_.patrolPoints[aiBot_.patrolIndex];
    const glm::vec3 toPatrol = patrolTarget - aiBot_.controller.GetPosition();
    const glm::vec3 patrolDir = NormalizeHorizontalOrZero(toPatrol);
    const float distanceToPatrol = glm::length(glm::vec2(toPatrol.x, toPatrol.z));
    if (distanceToPatrol <= aiPatrolPointRadius_)
    {
        aiBot_.patrolIndex = (aiBot_.patrolIndex + 1) % aiBot_.patrolPoints.size();
        aiBot_.patrolPauseRemaining = aiPatrolPauseTime_;
        aiBot_.strafeSign *= -1.0f;
        return EBehaviorTreeStatus::Success;
    }

    aiBot_.lookDir = glm::length(patrolDir) > 0.001f ? patrolDir : aiBot_.lookDir;
    aiBot_.moveDir = patrolDir;
    return EBehaviorTreeStatus::Running;
}

void CharacterDemoGameInstance::UpdateAIBotNode()
{
    const glm::vec3 position = aiBot_.controller.GetPosition();
    const glm::quat rotation = glm::angleAxis(aiBot_.yaw, glm::vec3(0.0f, 1.0f, 0.0f));

    if (aiBot_.visualNode)
    {
        aiBot_.visualNode->SetTranslation(position);
        aiBot_.visualNode->SetRotation(rotation);
        aiBot_.visualNode->RecalcTransform(true);
    }

    if (aiBot_.skinnedRoot)
    {
        aiBot_.skinnedRoot->SetTranslation(position);
        aiBot_.skinnedRoot->SetRotation(rotation);
        aiBot_.skinnedRoot->RecalcTransform(true);
    }

    if (!aiBot_.visualNode && !aiBot_.skinnedRoot)
    {
        return;
    }
    engine_->GetScene().MarkDirty();
}

void CharacterDemoGameInstance::TryInitAIBotCharacterModel()
{
    if (!aiBot_.skinnedRoot)
    {
        aiBot_.skinnedRoot = FindAppendedCharacterRoot(engine_->GetScene(), characterAppendRootName_, 0, skinnedCharacterRoot_);
        if (aiBot_.skinnedRoot)
        {
            aiBot_.appendRootName = aiBot_.skinnedRoot->GetName();
        }
    }

    if (!aiBot_.skinnedRoot)
    {
        return;
    }

    aiBot_.skinnedMeshComps.clear();
    aiBot_.primarySkinnedMeshComp = nullptr;

    std::function<void(const std::shared_ptr<Assets::Node>&)> collectSkinnedMeshes;
    collectSkinnedMeshes = [this, &collectSkinnedMeshes](const std::shared_ptr<Assets::Node>& node)
    {
        if (!node)
        {
            return;
        }

        if (auto comp = node->GetComponent<Runtime::SkinnedMeshComponent>())
        {
            if (!aiBot_.primarySkinnedMeshComp)
            {
                aiBot_.primarySkinnedMeshComp = comp.get();
            }
            aiBot_.skinnedMeshComps.push_back(comp.get());
        }

        for (const auto& child : node->Children())
        {
            collectSkinnedMeshes(child);
        }
    };
    collectSkinnedMeshes(aiBot_.skinnedRoot);

    if (aiBot_.skinnedMeshComps.empty())
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
            for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.skinnedMeshComps)
            {
                skinnedMeshComp->AddAnimations(tracks);
            }
        }
        else
        {
            SPDLOG_WARN("Failed to load AI animation file: {}", animFile);
        }
    }

    if (animIdle_.empty() && aiBot_.primarySkinnedMeshComp)
    {
        MapAnimationNames(aiBot_.primarySkinnedMeshComp->GetAnimationNames());
    }

    if (aiBot_.visualNode)
    {
        DisableNodePhysicsRecursive(aiBot_.visualNode);
        engine_->GetScene().RemoveNodeByInstanceId(aiBot_.visualNode->GetInstanceId());
        aiBot_.visualNode.reset();
    }

    DisableNodePhysicsRecursive(aiBot_.skinnedRoot);
    SetNodeRayCastVisibilityRecursive(aiBot_.skinnedRoot, false);

    Runtime::SkinnedMeshComponent::FootPlacementIKSettings footPlacementSettings;
    footPlacementSettings.Enabled = footIKEnabled_;
    footPlacementSettings.Weight = aiBot_.controller.IsOnGround() ? 1.0f : 0.0f;
    footPlacementSettings.TraceUpDistance = 0.45f;
    footPlacementSettings.TraceDownDistance = 0.90f;
    footPlacementSettings.FootHeight = 0.025f;
    footPlacementSettings.MaxFootLift = 0.28f;
    footPlacementSettings.MaxFootDrop = 0.35f;
    footPlacementSettings.PelvisWeight = 0.75f;
    footPlacementSettings.PelvisMaxOffset = 0.22f;
    footPlacementSettings.DebugDraw = showFootIKDebug_;
    for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.skinnedMeshComps)
    {
        skinnedMeshComp->SetFootPlacementIKSettings(footPlacementSettings);
    }

    if (!animIdle_.empty())
    {
        for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.skinnedMeshComps)
        {
            skinnedMeshComp->SetPlaySpeed(1.0f);
            skinnedMeshComp->PlayAnimation(animIdle_, true);
        }
    }

    aiBot_.characterModelLoaded = true;
    SPDLOG_INFO("AI character model initialized: root='{}', skinned meshes={}",
                aiBot_.skinnedRoot->GetName(), aiBot_.skinnedMeshComps.size());
}

void CharacterDemoGameInstance::UpdateAIBotAnimationState(float deltaSeconds)
{
    if (aiBot_.skinnedMeshComps.empty())
    {
        return;
    }

    glm::vec3 velocity = aiBot_.controller.GetLinearVelocity();
    const float horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
    const bool onGround = aiBot_.controller.IsOnGround();

    aiBot_.jumpStartHoldTimeRemaining = std::max(0.0f, aiBot_.jumpStartHoldTimeRemaining - deltaSeconds);
    aiBot_.jumpLandHoldTimeRemaining = std::max(0.0f, aiBot_.jumpLandHoldTimeRemaining - deltaSeconds);

    if (onGround && !aiBot_.wasOnGroundLastFrame)
    {
        aiBot_.jumpLandHoldTimeRemaining = jumpLandHoldTime_;
    }
    if (!onGround && aiBot_.wasOnGroundLastFrame && velocity.y > 0.1f)
    {
        aiBot_.jumpStartHoldTimeRemaining = jumpStartHoldTime_;
        aiBot_.jumpLandHoldTimeRemaining = 0.0f;
    }

    const glm::vec2 horizontalVelocity(velocity.x, velocity.z);
    const float desiredSpeed =
        aiBot_.state == EAIBotState::Chase
            ? aiRunSpeed_
            : (aiBot_.state == EAIBotState::Evade
                   ? aiRunSpeed_
                   : (aiBot_.state == EAIBotState::Attack ? std::max(aiWalkSpeed_, aiRunSpeed_ * 0.85f) : aiWalkSpeed_));
    const glm::vec3 commandedMoveDir = NormalizeHorizontalOrZero(aiBot_.moveDir);
    const glm::vec2 commandedHorizontalVelocity(commandedMoveDir.x * desiredSpeed, commandedMoveDir.z * desiredSpeed);
    const glm::vec3 botForward(std::sin(aiBot_.yaw), 0.0f, std::cos(aiBot_.yaw));
    const glm::vec3 botRight(-botForward.z, 0.0f, botForward.x);
    const glm::vec2 forward2D = glm::normalize(glm::vec2(botForward.x, botForward.z));
    const glm::vec2 right2D = glm::normalize(glm::vec2(botRight.x, botRight.z));
    const float actualForwardSpeed = glm::dot(horizontalVelocity, forward2D);
    const float actualRightSpeed = glm::dot(horizontalVelocity, right2D);
    const float intendedForwardSpeed = glm::dot(commandedHorizontalVelocity, forward2D);
    const float intendedRightSpeed = glm::dot(commandedHorizontalVelocity, right2D);
    const float localForwardSpeed =
        std::abs(actualForwardSpeed) >= std::abs(intendedForwardSpeed) * 0.65f ? actualForwardSpeed : intendedForwardSpeed;
    const float localRightSpeed =
        std::abs(actualRightSpeed) >= std::abs(intendedRightSpeed) * 0.65f ? actualRightSpeed : intendedRightSpeed;
    const float movementSignal = std::max(horizontalSpeed, glm::length(commandedHorizontalVelocity));

    ECharacterAnimState newState = ECharacterAnimState::Idle;
    std::string animationToPlay = animIdle_;
    bool loop = true;
    float playSpeed = 1.0f;

    if (!onGround)
    {
        if (aiBot_.jumpStartHoldTimeRemaining > 0.0f && velocity.y >= 0.0f)
        {
            newState = ECharacterAnimState::JumpStart;
            animationToPlay = animJumpStart_;
            loop = false;
        }
        else
        {
            newState = ECharacterAnimState::JumpLoop;
            animationToPlay = animJumpLoop_;
        }
    }
    else if (aiBot_.jumpLandHoldTimeRemaining > 0.0f)
    {
        newState = ECharacterAnimState::JumpLand;
        animationToPlay = animJumpLand_;
        loop = false;
    }
    else if (movementSignal > 0.35f)
    {
        if (aiBot_.state == EAIBotState::Chase)
        {
            newState = ECharacterAnimState::RunForward;
            animationToPlay = animRunForward_;
        }
        else if (aiBot_.state == EAIBotState::Evade && localForwardSpeed < -0.2f)
        {
            newState = ECharacterAnimState::RunBackward;
            animationToPlay = animRunBackward_;
            playSpeed = runBackwardPlaySpeed_;
        }
        else if (std::abs(localRightSpeed) > std::abs(localForwardSpeed) * 1.1f)
        {
            const bool movingRight = localRightSpeed > 0.0f;
            newState = movingRight ? ECharacterAnimState::RunStrafeRight : ECharacterAnimState::RunStrafeLeft;
            animationToPlay = movingRight ? animRunStrafeRight_ : animRunStrafeLeft_;
        }
        else if (localForwardSpeed < -0.2f)
        {
            newState = ECharacterAnimState::WalkBackward;
            animationToPlay = animWalkBackward_;
        }
        else if (aiBot_.state == EAIBotState::Patrol)
        {
            newState = ECharacterAnimState::WalkForward;
            animationToPlay = animWalkForward_;
        }
        else
        {
            newState = ECharacterAnimState::RunForward;
            animationToPlay = animRunForward_;
        }
    }

    aiBot_.wasOnGroundLastFrame = onGround;

    if (newState != aiBot_.animState ||
        (loop && aiBot_.primarySkinnedMeshComp &&
         aiBot_.primarySkinnedMeshComp->GetCurrentAnimationName() != animationToPlay))
    {
        aiBot_.animState = newState;
        for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.skinnedMeshComps)
        {
            skinnedMeshComp->SetPlaySpeed(playSpeed);
            skinnedMeshComp->PlayAnimation(animationToPlay, loop);
        }
    }

    const float ikWeight = footIKEnabled_ && onGround && aiBot_.animState == ECharacterAnimState::Idle ? 1.0f : 0.0f;
    for (Runtime::SkinnedMeshComponent* skinnedMeshComp : aiBot_.skinnedMeshComps)
    {
        skinnedMeshComp->SetFootPlacementIKEnabled(footIKEnabled_);
        skinnedMeshComp->SetFootPlacementIKWeight(ikWeight);
        auto settings = skinnedMeshComp->GetFootPlacementIKSettings();
        settings.DebugDraw = showFootIKDebug_;
        skinnedMeshComp->SetFootPlacementIKSettings(settings);
    }
}

void CharacterDemoGameInstance::TryInitCharacterModel()
{
    if (!skinnedCharacterRoot_)
    {
        skinnedCharacterRoot_ = FindAppendedCharacterRoot(engine_->GetScene(), characterAppendRootName_, 0);
    }

    if (!skinnedCharacterRoot_)
    {
        return; // Not loaded yet, try again next tick
    }

    skinnedMeshComps_.clear();
    primarySkinnedMeshComp_ = nullptr;

    std::function<void(const std::shared_ptr<Assets::Node>&)> collectSkinnedMeshes;
    collectSkinnedMeshes = [this, &collectSkinnedMeshes](const std::shared_ptr<Assets::Node>& node)
    {
        if (!node)
        {
            return;
        }

        if (auto comp = node->GetComponent<Runtime::SkinnedMeshComponent>())
        {
            if (!primarySkinnedMeshComp_)
            {
                primarySkinnedMeshComp_ = comp.get();
            }
            skinnedMeshComps_.push_back(comp.get());
        }

        for (const auto& child : node->Children())
        {
            collectSkinnedMeshes(child);
        }
    };
    collectSkinnedMeshes(skinnedCharacterRoot_);

    if (skinnedMeshComps_.empty())
    {
        return; // Append root exists, but components are not fully attached yet.
    }

    SPDLOG_INFO("Character model root '{}' found with {} skinned mesh nodes, loading animation packs...",
                skinnedCharacterRoot_->GetName(), skinnedMeshComps_.size());

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
            for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps_)
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
    auto names = primarySkinnedMeshComp_->GetAnimationNames();
    for (const auto& name : names)
    {
        SPDLOG_INFO("Character animation: '{}'", name);
    }
    MapAnimationNames(names);

    // Remove the temporary placeholder once the real character is ready.
    if (characterNode_)
    {
        DisableNodePhysicsRecursive(characterNode_);
        engine_->GetScene().RemoveNodeByInstanceId(characterNode_->GetInstanceId());
        characterNode_.reset();
    }

    // Character collision should come only from the controller, not the imported skinned mesh hierarchy.
    // Otherwise the appended mannequin can leave kinematic mesh colliders around the origin / T-pose.
    DisableNodePhysicsRecursive(skinnedCharacterRoot_);

    // Apply current first-person visibility and start idle animation
    SetFirstPersonMode(firstPersonMode_);
    SetNodeRayCastVisibilityRecursive(skinnedCharacterRoot_, false);

    Runtime::SkinnedMeshComponent::FootPlacementIKSettings footPlacementSettings;
    footPlacementSettings.Enabled = footIKEnabled_;
    footPlacementSettings.Weight = characterController_.IsOnGround() ? 1.0f : 0.0f;
    footPlacementSettings.TraceUpDistance = 0.45f;
    footPlacementSettings.TraceDownDistance = 0.90f;
    footPlacementSettings.FootHeight = 0.025f;
    footPlacementSettings.MaxFootLift = 0.28f;
    footPlacementSettings.MaxFootDrop = 0.35f;
    footPlacementSettings.PelvisWeight = 0.75f;
    footPlacementSettings.PelvisMaxOffset = 0.22f;
    footPlacementSettings.DebugDraw = showFootIKDebug_;
    for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps_)
    {
        skinnedMeshComp->SetFootPlacementIKSettings(footPlacementSettings);
    }

    if (!animIdle_.empty())
    {
        PlayCharacterAnimation(animIdle_, true);
    }

    characterModelLoaded_ = true;
    SPDLOG_INFO("Character model initialized: root='{}', animations={}",
                skinnedCharacterRoot_->GetName(), names.size());
}

void CharacterDemoGameInstance::MapAnimationNames(const std::vector<std::string>& names)
{
    auto findFirst = [&names](std::initializer_list<const char*> candidates) -> std::string
    {
        for (const char* candidate : candidates)
        {
            auto it = std::find(names.begin(), names.end(), candidate);
            if (it != names.end())
            {
                return *it;
            }
        }
        return {};
    };

    // Exact mapping based on the three 3C-oriented packs:
    // General: Idle_A / Idle_B
    // MovementBasic: Walking_A/B/C, Running_A/B, Jump_Start/Idle/Land
    // MovementAdvanced: Walking_Backwards, Running_Strafe_Left/Right
    animIdle_ = findFirst({"Idle_A", "Idle_B"});
    animWalkForward_ = findFirst({"Walking_A", "Walking_B", "Walking_C"});
    animWalkBackward_ = findFirst({"Walking_Backwards", "Walking_B", "Walking_A"});
    animStrafeLeft_ = findFirst({"Running_Strafe_Left", "Dodge_Left"});
    animStrafeRight_ = findFirst({"Running_Strafe_Right", "Dodge_Right"});
    animRunForward_ = findFirst({"Running_A", "Running_B"});
    animRunBackward_ = animWalkBackward_;
    animRunStrafeLeft_ = animStrafeLeft_;
    animRunStrafeRight_ = animStrafeRight_;
    animJumpStart_ = findFirst({"Jump_Start", "Jump_Full_Short", "Jump_Full_Long"});
    animJumpLoop_ = findFirst({"Jump_Idle", "Jump_Full_Long", "Jump_Full_Short"});
    animJumpLand_ = findFirst({"Jump_Land", "Jump_Full_Short"});

    if (animIdle_.empty() && !names.empty())
    {
        animIdle_ = names.front();
    }
    if (animWalkForward_.empty())
    {
        animWalkForward_ = animIdle_;
    }
    if (animWalkBackward_.empty())
    {
        animWalkBackward_ = animWalkForward_;
    }
    if (animStrafeLeft_.empty())
    {
        animStrafeLeft_ = animWalkForward_;
    }
    if (animStrafeRight_.empty())
    {
        animStrafeRight_ = animWalkForward_;
    }
    if (animRunForward_.empty())
    {
        animRunForward_ = animWalkForward_;
    }
    if (animRunBackward_.empty())
    {
        animRunBackward_ = animWalkBackward_;
    }
    if (animRunStrafeLeft_.empty())
    {
        animRunStrafeLeft_ = animStrafeLeft_;
    }
    if (animRunStrafeRight_.empty())
    {
        animRunStrafeRight_ = animStrafeRight_;
    }
    if (animJumpStart_.empty())
    {
        animJumpStart_ = animJumpLoop_.empty() ? animIdle_ : animJumpLoop_;
    }
    if (animJumpLoop_.empty())
    {
        animJumpLoop_ = animJumpStart_;
    }
    if (animJumpLand_.empty())
    {
        animJumpLand_ = animJumpLoop_;
    }

    SPDLOG_INFO(
        "3C animation mapping: Idle='{}', WalkF='{}', WalkB='{}', StrafeL='{}', StrafeR='{}', RunF='{}', JumpStart='{}', JumpLoop='{}', JumpLand='{}'",
        animIdle_, animWalkForward_, animWalkBackward_, animStrafeLeft_, animStrafeRight_, animRunForward_,
        animJumpStart_, animJumpLoop_, animJumpLand_);
}

void CharacterDemoGameInstance::UpdateAnimationState(float deltaSeconds)
{
    if (skinnedMeshComps_.empty())
    {
        return;
    }

    glm::vec3 vel = characterController_.GetLinearVelocity();
    float horizontalSpeed = glm::length(glm::vec2(vel.x, vel.z));
    bool onGround = characterController_.IsOnGround();

    jumpStartHoldTimeRemaining_ = std::max(0.0f, jumpStartHoldTimeRemaining_ - deltaSeconds);
    jumpLandHoldTimeRemaining_ = std::max(0.0f, jumpLandHoldTimeRemaining_ - deltaSeconds);

    const glm::vec2 horizontalVelocity(vel.x, vel.z);
    const glm::vec2 forward2D = glm::normalize(glm::vec2(GetMoveForward().x, GetMoveForward().z));
    const glm::vec2 right2D = glm::normalize(glm::vec2(GetMoveRight().x, GetMoveRight().z));
    const float localForwardSpeed = glm::dot(horizontalVelocity, forward2D);
    const float localRightSpeed = glm::dot(horizontalVelocity, right2D);

    ECharacterAnimState newState = ECharacterAnimState::Idle;
    std::string animationToPlay = animIdle_;
    bool loop = true;
    float playSpeed = 1.0f;

    if (onGround && !wasOnGroundLastFrame_)
    {
        jumpLandHoldTimeRemaining_ = jumpLandHoldTime_;
    }
    if (!onGround && wasOnGroundLastFrame_ && vel.y > 0.1f)
    {
        jumpStartHoldTimeRemaining_ = jumpStartHoldTime_;
        jumpLandHoldTimeRemaining_ = 0.0f;
    }

    if (!onGround)
    {
        if (jumpStartHoldTimeRemaining_ > 0.0f && vel.y >= 0.0f)
        {
            newState = ECharacterAnimState::JumpStart;
            animationToPlay = animJumpStart_;
            loop = false;
        }
        else
        {
            newState = ECharacterAnimState::JumpLoop;
            animationToPlay = animJumpLoop_;
        }
    }
    else if (jumpLandHoldTimeRemaining_ > 0.0f)
    {
        newState = ECharacterAnimState::JumpLand;
        animationToPlay = animJumpLand_;
        loop = false;
    }
    else if (horizontalSpeed > 0.35f)
    {
        if (!firstPersonMode_ && movementMode_ == ECharacterMovementMode::MoveAligned)
        {
            if (keySprint_)
            {
                newState = ECharacterAnimState::RunForward;
                animationToPlay = animRunForward_;
            }
            else
            {
                newState = ECharacterAnimState::WalkForward;
                animationToPlay = animWalkForward_;
            }
        }
        else
        {
            const float absForward = std::abs(localForwardSpeed);
            const float absRight = std::abs(localRightSpeed);
            const bool strafeDominant = absRight > absForward * 1.1f;
            const bool backwardDominant = !strafeDominant && localForwardSpeed < -0.2f;
            const bool sprinting = keySprint_ && localForwardSpeed > 0.2f;

            if (strafeDominant)
            {
                const bool moveRight = localRightSpeed > 0.0f;
                if (keySprint_)
                {
                    newState = moveRight ? ECharacterAnimState::RunStrafeRight : ECharacterAnimState::RunStrafeLeft;
                    animationToPlay = moveRight ? animRunStrafeRight_ : animRunStrafeLeft_;
                    playSpeed = 1.0f;
                }
                else
                {
                    newState = moveRight ? ECharacterAnimState::WalkStrafeRight : ECharacterAnimState::WalkStrafeLeft;
                    animationToPlay = moveRight ? animStrafeRight_ : animStrafeLeft_;
                    playSpeed = walkStrafePlaySpeed_;
                }
            }
            else if (backwardDominant)
            {
                if (keySprint_)
                {
                    newState = ECharacterAnimState::RunBackward;
                    animationToPlay = animRunBackward_;
                    playSpeed = runBackwardPlaySpeed_;
                }
                else
                {
                    newState = ECharacterAnimState::WalkBackward;
                    animationToPlay = animWalkBackward_;
                }
            }
            else
            {
                if (sprinting)
                {
                    newState = ECharacterAnimState::RunForward;
                    animationToPlay = animRunForward_;
                }
                else
                {
                    newState = ECharacterAnimState::WalkForward;
                    animationToPlay = animWalkForward_;
                }
            }
        }
    }

    wasOnGroundLastFrame_ = onGround;

    if (newState != currentAnimState_)
    {
        currentAnimState_ = newState;
        PlayCharacterAnimation(animationToPlay, loop, playSpeed);
        return;
    }

    // Keep directional fallback states responsive when they share the same source clip but a different speed profile.
    if (loop && primarySkinnedMeshComp_ && primarySkinnedMeshComp_->GetCurrentAnimationName() != animationToPlay)
    {
        PlayCharacterAnimation(animationToPlay, loop, playSpeed);
    }
}

void CharacterDemoGameInstance::ResetCharacterState()
{
    aiBot_.controller.Destroy();
    aiBot_.visualNode.reset();
    aiBot_.skinnedRoot.reset();
    aiBot_.primarySkinnedMeshComp = nullptr;
    aiBot_.skinnedMeshComps.clear();
    aiBot_.patrolPoints.clear();
    aiBot_.moveDir = glm::vec3(0.0f);
    aiBot_.lookDir = glm::vec3(0.0f, 0.0f, 1.0f);
    aiBot_.lastKnownTargetPosition = glm::vec3(0.0f);
    aiBot_.yaw = 0.0f;
    aiBot_.fireCooldownRemaining = 0.0f;
    aiBot_.targetMemoryRemaining = 0.0f;
    aiBot_.patrolPauseRemaining = 0.0f;
    aiBot_.strafeSign = 1.0f;
    aiBot_.patrolIndex = 0;
    aiBot_.targetVisible = false;
    aiBot_.triggerJump = false;
    aiBot_.state = EAIBotState::Disabled;
    aiBot_.animState = ECharacterAnimState::Idle;
    aiBot_.characterModelLoaded = false;
    aiBot_.characterLoadRequested = false;
    aiBot_.wasOnGroundLastFrame = true;
    aiBot_.jumpStartHoldTimeRemaining = 0.0f;
    aiBot_.jumpLandHoldTimeRemaining = 0.0f;

    skinnedCharacterRoot_.reset();
    primarySkinnedMeshComp_ = nullptr;
    skinnedMeshComps_.clear();
    characterModelLoaded_ = false;
    characterLoadRequested_ = false;
    currentAnimState_ = ECharacterAnimState::Idle;
    animIdle_.clear();
    animWalkForward_.clear();
    animWalkBackward_.clear();
    animStrafeLeft_.clear();
    animStrafeRight_.clear();
    animRunForward_.clear();
    animRunBackward_.clear();
    animRunStrafeLeft_.clear();
    animRunStrafeRight_.clear();
    animJumpStart_.clear();
    animJumpLoop_.clear();
    animJumpLand_.clear();
    wasOnGroundLastFrame_ = true;
    jumpStartHoldTimeRemaining_ = 0.0f;
    jumpLandHoldTimeRemaining_ = 0.0f;
    characterYaw_ = yaw_;
}

void CharacterDemoGameInstance::SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
{
    if (!node)
    {
        return;
    }

    if (auto renderComp = node->GetComponent<Runtime::RenderComponent>())
    {
        renderComp->SetVisible(visible);
    }

    for (const auto& child : node->Children())
    {
        SetNodeVisibilityRecursive(child, visible);
    }
}

void CharacterDemoGameInstance::SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
{
    if (!node)
    {
        return;
    }

    if (auto renderComp = node->GetComponent<Runtime::RenderComponent>())
    {
        renderComp->SetRayCastVisible(visible);
    }

    for (const auto& child : node->Children())
    {
        SetNodeRayCastVisibilityRecursive(child, visible);
    }
}

void CharacterDemoGameInstance::DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node)
{
    if (!node)
    {
        return;
    }

    if (auto physComp = node->GetComponent<Runtime::PhysicsComponent>())
    {
        if (NextPhysics* physicsEngine = engine_->GetPhysicsEngine())
        {
            const NextBodyID bodyId = physComp->GetPhysicsBody();
            if (!bodyId.IsInvalid())
            {
                physicsEngine->RemoveBody(bodyId);
            }
        }

        auto disabledPhysicsComp = std::make_shared<Runtime::PhysicsComponent>();
        disabledPhysicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
        disabledPhysicsComp->SetPhysicsOffset(physComp->GetPhysicsOffset());
        node->AddComponent(disabledPhysicsComp);
    }

    for (const auto& child : node->Children())
    {
        DisableNodePhysicsRecursive(child);
    }
}

void CharacterDemoGameInstance::PlayCharacterAnimation(const std::string& name, bool loop, float playSpeed)
{
    if (name.empty())
    {
        return;
    }

    for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps_)
    {
        skinnedMeshComp->SetPlaySpeed(playSpeed);
        skinnedMeshComp->PlayAnimation(name, loop);
    }
}

void CharacterDemoGameInstance::UpdateCharacterAnimationPostProcess()
{
    if (skinnedMeshComps_.empty())
    {
        return;
    }

    const float ikWeight =
        footIKEnabled_ && (characterController_.IsOnGround() && currentAnimState_ == ECharacterAnimState::Idle) ? 1.0f : 0.0f;

    for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps_)
    {
        skinnedMeshComp->SetFootPlacementIKEnabled(footIKEnabled_);
        skinnedMeshComp->SetFootPlacementIKWeight(ikWeight);
        auto settings = skinnedMeshComp->GetFootPlacementIKSettings();
        settings.DebugDraw = showFootIKDebug_;
        skinnedMeshComp->SetFootPlacementIKSettings(settings);
    }
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

void CharacterDemoGameInstance::DrawAIBotBehaviorTreeUI() const
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 overlaySize(760.0f, 360.0f);
    const ImVec2 overlayPos(
        viewport->WorkPos.x + viewport->WorkSize.x - overlaySize.x - 18.0f,
        viewport->WorkPos.y + 14.0f);

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
    if (aiBot_.controller.IsValid())
    {
        const glm::vec3 playerPos = characterController_.GetPosition();
        const glm::vec3 aiPos = aiBot_.controller.GetPosition();
        const float distance = glm::length(glm::vec2(playerPos.x - aiPos.x, playerPos.z - aiPos.z));
        summary += fmt::format("  |  Dist {:.1f}  |  Visible {}  |  LOS {}",
                               distance,
                               aiBot_.targetVisible ? "Yes" : "No",
                               HasLineOfSightToPlayer() ? "Yes" : "No");
    }
    drawList->AddText(ImVec2(base.x + 18.0f, base.y + 52.0f), IM_COL32(215, 221, 228, 230), summary.c_str());

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
    switch (currentAnimState_)
    {
    case ECharacterAnimState::Idle: return "Idle";
    case ECharacterAnimState::WalkForward: return "WalkForward";
    case ECharacterAnimState::WalkBackward: return "WalkBackward";
    case ECharacterAnimState::WalkStrafeLeft: return "WalkStrafeLeft";
    case ECharacterAnimState::WalkStrafeRight: return "WalkStrafeRight";
    case ECharacterAnimState::RunForward: return "RunForward";
    case ECharacterAnimState::RunBackward: return "RunBackward";
    case ECharacterAnimState::RunStrafeLeft: return "RunStrafeLeft";
    case ECharacterAnimState::RunStrafeRight: return "RunStrafeRight";
    case ECharacterAnimState::JumpStart: return "JumpStart";
    case ECharacterAnimState::JumpLoop: return "JumpLoop";
    case ECharacterAnimState::JumpLand: return "JumpLand";
    default: return "Unknown";
    }
}
