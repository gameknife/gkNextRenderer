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
#include "Runtime/Utilities/PhysicsDebugOverlay.hpp"
#include "Vulkan/WindowSurface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<CharacterDemoGameInstance>(config, options, engine);
}

CharacterDemoGameInstance::CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
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
}

void CharacterDemoGameInstance::OnDestroy()
{
    characterController_.Destroy();
}

void CharacterDemoGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
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
    settings.height = 1.75f;
    settings.radius = 0.3f;
    settings.maxStrength = 2000.0f;

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
    SetFirstPersonMode(firstPersonMode_);
    engine_->GetScene().MarkDirty();

    // Load the skinned character model asynchronously
    engine_->RequestLoadSceneAdd("assets/models/characters/Mannequin_Medium.glb");
    characterLoadRequested_ = true;

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
    ImGui::Text("Physics Debug: %s", showPhysicsDebug_ ? "On" : "Off");
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
    ImGui::Separator();
    ImGui::Text("WASD - Move | Shift - Run");
    ImGui::Text("Space - Jump | Mouse - Look");
    ImGui::Text("V - Toggle FPS/TPS | Tab - Move Mode");
    ImGui::Text("LMB - Shoot | F1 - Physics Debug");
    ImGui::Text("ESC - Release Mouse");

    ImGui::SliderFloat("Walk Speed", &walkSpeed_, 1.0f, 10.0f);
    ImGui::SliderFloat("Run Speed", &runSpeed_, 5.0f, 20.0f);
    ImGui::SliderFloat("Camera Dist", &cameraDistance_, 1.0f, 15.0f);

    ImGui::End();

    if (showPhysicsDebug_)
    {
        Runtime::DrawPhysicsDebugOverlay(engine_->GetScene(), engine_->GetScene().GetRenderCamera());
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
    return characterController_.GetPosition() + glm::vec3(0.0f, firstPersonEyeHeight_, 0.0f);
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

    const uint32_t instanceId = engine_->GetScene().GenerateInstanceId();
    auto newNode = Assets::Node::CreateNode(
        "ShotBox",
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

    engine_->GetPhysicsEngine()->AddForceToBody(bodyId, shotDir * projectileForce_);
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

void CharacterDemoGameInstance::TryInitCharacterModel()
{
    if (!skinnedCharacterRoot_)
    {
        for (auto& node : engine_->GetScene().Nodes())
        {
            if (node->GetParent() == nullptr && node->GetName() == characterAppendRootName_)
            {
                skinnedCharacterRoot_ = node;
                break;
            }
        }
    }

    if (!skinnedCharacterRoot_)
    {
        for (auto& node : engine_->GetScene().Nodes())
        {
            if (node->GetParent() == nullptr &&
                node->GetName().starts_with(characterAppendRootName_ + "_"))
            {
                skinnedCharacterRoot_ = node;
                break;
            }
        }
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
        engine_->GetScene().RemoveNodeByInstanceId(characterNode_->GetInstanceId());
        characterNode_.reset();
    }

    // Apply current first-person visibility and start idle animation
    SetFirstPersonMode(firstPersonMode_);
    if (!animIdle_.empty())
    {
        PlayCharacterAnimation(animIdle_, true);
    }

    characterModelLoaded_ = true;
    SPDLOG_INFO("Character model initialized with {} animations", names.size());
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
