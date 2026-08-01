#include "Engine/Runtime/GameInstance.hpp"
#include "BrickPlayerGameInstance.hpp"
#include "BrickPlayerSnapLogic.hpp"
#include "BrickPlayerUserInterface.hpp"
#include "Modules/LDrawLoader/FLDrawLoader.h"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <SDL3/SDL_dialog.h>
#include <spdlog/spdlog.h>
#include <glm/ext/scalar_constants.hpp>
#include <filesystem>
#include <system_error>
#include "Modules/LDrawLoader/LDrawModule.hpp"

namespace
{
    struct WorldBounds
    {
        glm::vec3 min{FLT_MAX};
        glm::vec3 max{-FLT_MAX};
    };

    struct PhysicsBodyRef
    {
        Runtime::PhysicsComponent* physComp = nullptr;
        FNextPhysicsBody* body = nullptr;
        explicit operator bool() const { return body != nullptr; }
    };

    PhysicsBodyRef GetPhysicsBody([[maybe_unused]] Assets::Node* node)
    {
        if (!node)
            return {};
        auto comp = node->GetComponent<Runtime::PhysicsComponent>();
        if (!comp)
            return {};
        auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
        if (!physics)
            return {};
        auto* body = physics->GetBody(comp->GetPhysicsBody());
        if (!body)
            return {};
        return {comp.get(), body};
    }

    struct PlaySpeedPreset
    {
        const char* label;
        float interval;
    };

    constexpr PlaySpeedPreset playSpeedPresets[] = {
        {"0.5x", 0.5f},
        {"1x",   0.2f},
        {"2x",   0.1f},
        {"5x",   0.04f},
        {"10x",  0.02f},
    };

    constexpr int32_t playSpeedCount = static_cast<int32_t>(std::size(playSpeedPresets));

    const char* GetRandomPutSoundPath()
    {
        switch (std::rand() % 3)
        {
        case 0:
            return "assets/sfx/put1.wav";
        case 1:
            return "assets/sfx/put2.wav";
        default:
            return "assets/sfx/put3.wav";
        }
    }

    float AxisGap(float minA, float maxA, float minB, float maxB)
    {
        if (maxA < minB)
        {
            return minB - maxA;
        }
        if (maxB < minA)
        {
            return minA - maxB;
        }
        return 0.0f;
    }

    WorldBounds TransformLocalBounds(const glm::mat4& worldTransform, const glm::vec3& localMin, const glm::vec3& localMax)
    {
        const glm::vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z}
        };

        WorldBounds bounds;
        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 worldPoint = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));
            bounds.min = glm::min(bounds.min, worldPoint);
            bounds.max = glm::max(bounds.max, worldPoint);
        }

        return bounds;
    }

    float ProjectHalfExtentOnAxis(const glm::vec3& halfExtent, const glm::quat& rotation, const glm::vec3& axis)
    {
        const glm::vec3 normalizedAxis = glm::dot(axis, axis) > 1e-6f
            ? glm::normalize(axis)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::mat3 rotationMatrix = glm::mat3_cast(rotation);
        const glm::vec3 localAxis = glm::transpose(rotationMatrix) * normalizedAxis;
        const glm::vec3 absLocalAxis = glm::abs(localAxis);
        return glm::dot(absLocalAxis, halfExtent);
    }

    bool IntersectHorizontalPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir, float planeY, glm::vec3& outPoint)
    {
        if (glm::abs(rayDir.y) < 1e-4f)
        {
            return false;
        }

        const float t = (planeY - rayOrigin.y) / rayDir.y;
        if (t < 0.0f)
        {
            return false;
        }

        outPoint = rayOrigin + rayDir * t;
        return true;
    }

    uint32_t HashLoosePartSeed(uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        value *= 0x846ca68bU;
        value ^= value >> 16;
        return value;
    }

    float HashToSignedUnit(uint32_t value)
    {
        const float normalized = static_cast<float>(HashLoosePartSeed(value) & 0xffffU) / 65535.0f;
        return normalized * 2.0f - 1.0f;
    }

    void WakeLoosePartBody(NextPhysics* physics, const NextBodyID& bodyId, uint32_t instanceId)
    {
        if (!physics || bodyId.IsInvalid())
        {
            return;
        }

        const glm::vec3 linearVelocity(
            HashToSignedUnit(instanceId ^ 0x13572468U) * 0.08f,
            -0.18f,
            HashToSignedUnit(instanceId ^ 0x24681357U) * 0.08f);
        const glm::vec3 angularVelocity(
            HashToSignedUnit(instanceId ^ 0x89abcdefU) * 1.2f,
            HashToSignedUnit(instanceId ^ 0xfedcba98U) * 2.0f,
            HashToSignedUnit(instanceId ^ 0x31415926U) * 1.2f);
        physics->SetBodyVelocity(bodyId, linearVelocity, angularVelocity);
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    return std::make_unique<BrickPlayerGameInstance>(config, options, engine);
}

BrickPlayerGameInstance::PhysicsBodyResult BrickPlayerGameInstance::CreateDynamicPhysicsBody(
    Assets::Node* node, const glm::vec3& worldScale, const glm::quat& worldRotation)
{
    PhysicsBodyResult result;
    if (!node)
        return result;

    auto render = node->GetComponent<Runtime::RenderComponent>();
    if (!render || !render->IsDrawable())
        return result;

    const auto* model = GetEngine().GetScene().GetModel(render->GetModelId());
    if (!model)
        return result;

    const glm::vec3 aabbMin = model->GetLocalAABBMin();
    const glm::vec3 aabbMax = model->GetLocalAABBMax();
    const glm::vec3 fullExtent = glm::max((aabbMax - aabbMin) * glm::abs(worldScale), glm::vec3(0.002f));
    result.halfExtent = fullExtent * 0.5f;
    const glm::vec3 aabbCenter = (aabbMin + aabbMax) * 0.5f;
    const glm::vec3 worldCenter = glm::vec3(node->WorldTransform() * glm::vec4(aabbCenter, 1.0f));

    // Remove existing physics body if any
    auto existingPhys = node->GetComponent<Runtime::PhysicsComponent>();
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (existingPhys && physics)
    {
        NextBodyID oldBody = existingPhys->GetPhysicsBody();
        if (!oldBody.IsInvalid())
            physics->RemoveBody(oldBody);
    }

    if (physics)
    {
        auto phys = std::make_shared<Runtime::PhysicsComponent>();
        phys->SetMobility(Runtime::ENodeMobility::Dynamic);
        auto bodyId = physics->CreateBoxBody(worldCenter, worldRotation, fullExtent, NextMotionType::Dynamic);
        phys->BindPhysicsBody(bodyId);
        phys->SetPhysicsOffset(aabbCenter);
        node->AddComponent(phys);
        result.created = true;
    }

    return result;
}

BrickPlayerGameInstance::BrickPlayerGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    config.Title = "BrickPlayer";
    config.Width = 1920;
    config.Height = 960;
    config.HideTitleBar = true;

    options.ForceSDR = true;

    userInterface_ = std::make_unique<BrickPlayerUserInterface>(this);

    const std::string magicalegoPakPath = Utilities::FileHelper::GetPlatformFilePath("assets/paks/magicalego.pak");
    std::error_code ec;
    if (std::filesystem::exists(magicalegoPakPath, ec))
    {
        GetEngine().GetPakSystem().MountPak(magicalegoPakPath);
    }
}

void BrickPlayerGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "16", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    // cvars.SetDefaultFromString("r.upscaler.type", "2", &error);
}

void BrickPlayerGameInstance::InitializeDefaultBGMPlaylist()
{
    bgmTracks_.clear();
    bgmTracks_.push_back({"Salut d'Amour", "assets/sfx/bgm.mp3"});
    bgmTracks_.push_back({"Liebestraum No. 3", "assets/sfx/bgm2.mp3"});
    currentBGM_ = bgmTracks_.empty()
        ? 0
        : static_cast<uint32_t>(bgmTracks_.size() - 1);
}

void BrickPlayerGameInstance::ResetDragState()
{
    isDraggingPart_ = false;
    draggedInstanceId_ = UINT32_MAX;
    dragPlanePoint_ = glm::vec3(0.0f);
    dragPlaneNormal_ = glm::vec3(0.0f, 0.0f, 1.0f);
    dragBodyOffset_ = glm::vec3(0.0f);
    dragReleaseLinearVelocity_ = glm::vec3(0.0f);
    lastDraggedBodyPosition_ = glm::vec3(0.0f);
    hasDraggedBodyPositionSample_ = false;
    lockedDraggedConnectorIndex_ = -1;
    activeSnapCandidate_ = {};
    snapFeedbackPulseUntil_ = 0.0f;
}

void BrickPlayerGameInstance::ClearHoverTargets(bool clearSceneHovered)
{
    hoveredDisassembled_.Clear();
    hoveredAssembly_.Clear();
    activeSnapCandidate_ = {};

    if (clearSceneHovered)
    {
        GetEngine().GetScene().ClearHoveredId();
    }
}

void BrickPlayerGameInstance::ResetInteractiveSceneState()
{
    disassembledNodes_.clear();
    originalAssemblyStates_.clear();
    selectedInstanceId_ = UINT32_MAX;
    selectedHitNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    hasFloorPlane_ = false;
    floorPlaneY_ = 0.0f;
    floorSurfaceY_ = 0.0f;

    ResetDragState();
    ClearHoverTargets(false);

    GetEngine().GetScene().ClearSelection();
    GetEngine().GetScene().ClearHoveredId();
    GetEngine().GetShowFlags().ShowEdge = false;
}

float BrickPlayerGameInstance::GetLduToWorldScale() const
{
    return Assets::SanitizeLDrawLduToWorldScale(GetEngine().GetUserSettings().LDrawLduToWorldScale);
}

int32_t BrickPlayerGameInstance::GetMaxTimelineStep() const
{
    return perPartMode_ ? totalParts_ : totalSteps_;
}

const BrickPlayerGameInstance::BGMTrack* BrickPlayerGameInstance::GetCurrentBGMTrack() const
{
    if (bgmTracks_.empty() || currentBGM_ >= bgmTracks_.size())
    {
        return nullptr;
    }

    return &bgmTracks_[currentBGM_];
}

void BrickPlayerGameInstance::FocusCameraOnLoadedScene()
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    glm::vec3 center{0.0f};
    int count = 0;

    auto& scene = GetEngine().GetScene();
    const auto& models = scene.Models();
    for (auto* render : scene.Components<Runtime::RenderComponent>())
    {
        Assets::Node* node = render->GetOwner();
        if (!node || !render->IsDrawable())
        {
            continue;
        }

        uint32_t modelIdx = render->GetModelId();
        if (modelIdx >= models.size())
        {
            continue;
        }

        const glm::vec3 worldPos = node->WorldTranslation();
        center += worldPos;
        count++;

        const auto& model = models[modelIdx];
        const glm::vec3 localMin = model.GetLocalAABBMin();
        const glm::vec3 localMax = model.GetLocalAABBMax();
        const glm::vec3 scale = node->WorldScale();
        minY = std::min(minY, worldPos.y + localMin.y * scale.y);
        maxY = std::max(maxY, worldPos.y + localMax.y * scale.y);
    }

    if (count <= 0)
    {
        return;
    }

    center /= static_cast<float>(count);
    cameraCenter_ = center;
    realCameraCenter_ = center;

    const float sceneHeight = maxY - minY;
    cameraArm_ = std::max(1.0f, sceneHeight * 3.0f);
}


void BrickPlayerGameInstance::UpdateAutoPlay(double deltaSeconds)
{
    if (!autoPlay_)
    {
        return;
    }

    const float interval = playSpeedPresets[playSpeedIndex_].interval;
    autoPlayTimer_ += static_cast<float>(deltaSeconds);
    if (autoPlayTimer_ < interval)
    {
        return;
    }

    autoPlayTimer_ -= interval;
    const int32_t maxStep = GetMaxTimelineStep();
    if (currentStep_ < maxStep - 1)
    {
        StepForward();
        return;
    }

    autoPlay_ = false;
}

void BrickPlayerGameInstance::SyncEdgeHighlight()
{
    GetEngine().GetShowFlags().ShowEdge =
        GetEngine().GetScene().GetHoveredId() != UINT32_MAX || selectedInstanceId_ != UINT32_MAX;
}

void BrickPlayerGameInstance::OnInit()
{
    InitializeDefaultBGMPlaylist();
    if (!bgmTracks_.empty())
    {
        PlayNextBGM();
    }

    if (!GOption->SceneName.empty())
    {
        currentScenePath_ = GOption->SceneName;
        GetEngine().RequestLoadScene({.filename = currentScenePath_});
    }
}

void BrickPlayerGameInstance::OnSceneLoaded()
{
    nodeStepMap_ = Assets::FLDrawLoader::GetLastLoadStepMap();
    nodePartFileMap_ = Assets::FLDrawLoader::GetLastLoadPartFileMap();
    totalSteps_ = Assets::FLDrawLoader::GetLastLoadTotalSteps();
    currentStep_ = 0;

    ResetInteractiveSceneState();

    CreateFloorPhysicsBody();
    FocusCameraOnLoadedScene();
    BuildPerPartOrder();
    CaptureOriginalAssemblyState();
    shadowLibrary_.Initialize(GetLduToWorldScale());

    // Auto-enable per-part mode if no build steps defined
    perPartMode_ = (totalSteps_ <= 1);

    // Detect FreeBuild mode
    isFreeBuildMode_ = Assets::FLDrawLoader::GetLastLoadIsFreeBuild();

    if (isFreeBuildMode_)
    {
        // Show all parts immediately — GetTotalSteps() accounts for perPartMode_
        currentStep_ = std::max(0, GetTotalSteps() - 1);
        BuildFreeBuildInventory();
    }

    UpdateVisibilityForStep(currentStep_, false);

    // In FreeBuild mode, make inventory bricks (step > 0) draggable;
    // baseplates (step 0) stay assembled as static snap targets.
    if (isFreeBuildMode_)
    {
        auto& scene = GetEngine().GetScene();
        for (auto* render : scene.Components<Runtime::RenderComponent>())
        {
            Assets::Node* node = render->GetOwner();
            if (!node)
                continue;
            uint32_t instanceId = node->GetInstanceId();
            auto stepIt = nodeStepMap_.find(instanceId);
            if (stepIt == nodeStepMap_.end())
                continue;
            // Step 0 = baseplate, keep assembled and non-draggable
            if (stepIt->second <= 0)
                continue;

            // Detach from any parent so physics operates in world space
            glm::vec3 worldTranslation = node->WorldTranslation();
            glm::quat worldRotation = node->WorldRotation();
            glm::vec3 worldScale = node->WorldScale();
            if (node->GetParent())
            {
                node->ClearParent();
                node->SetTranslation(worldTranslation);
                node->SetRotation(worldRotation);
                node->Scale() = worldScale;
                node->RecalcTransform(true);
            }

            auto bodyResult = CreateDynamicPhysicsBody(node, worldScale, worldRotation);
            if (!bodyResult.created)
                continue;
            auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
            if (physComp)
                WakeLoosePartBody(NextEngine::GetInstance()->GetPhysicsEngine(), physComp->GetPhysicsBody(), instanceId);

            disassembledNodes_[instanceId] = {bodyResult.halfExtent};
        }
        scene.MarkDirty();
    }

    sceneLoaded_ = true;

    SPDLOG_INFO("BrickPlayer: loaded scene with {} steps, {} parts, per-part mode: {}, freebuild: {}",
                totalSteps_, totalParts_, perPartMode_ ? "on" : "off", isFreeBuildMode_ ? "on" : "off");
}

void BrickPlayerGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneLoaded_)
        return;

    if (!isFreeBuildMode_)
        UpdateAutoPlay(deltaSeconds);

    PerformRaycast();

    if (isDraggingPart_)
    {
        UpdateDraggedPart();
    }

    SyncEdgeHighlight();

    // FreeBuild: auto-spawn when running low on loose bricks
    if (isFreeBuildMode_ && !isDraggingPart_)
    {
        int available = CountAvailableBricks();
        if (available < 6)
        {
            SpawnRandomBricks(12 - available);
        }
    }
}

void BrickPlayerGameInstance::OnInitUI()
{
    userInterface_->ApplyStyle();
}

bool BrickPlayerGameInstance::OnRenderUI()
{
    if (userInterface_)
        userInterface_->Render();
    return true;
}

bool BrickPlayerGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    float xRotation = cameraRotX_;
    float yRotation = cameraRotY_;
    float armLength = cameraArm_;

    glm::vec3 cameraPos;
    cameraPos.x = realCameraCenter_.x + armLength * cos(glm::radians(yRotation)) * cos(glm::radians(xRotation));
    cameraPos.y = realCameraCenter_.y + armLength * sin(glm::radians(yRotation));
    cameraPos.z = realCameraCenter_.z + armLength * cos(glm::radians(yRotation)) * sin(glm::radians(xRotation));

    glm::vec3 forward = glm::normalize(realCameraCenter_ - cameraPos);
    cachedCameraPos_ = cameraPos;
    forward.y = 0.0f;
    glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    left.y = 0.0f;

    panForward_ = glm::normalize(forward);
    panLeft_ = glm::normalize(left);

    OutRenderCamera.ModelView = glm::lookAtRH(cameraPos, realCameraCenter_, glm::vec3(0.0f, 1.0f, 0.0f));
    OutRenderCamera.FieldOfView = cameraFOV_;

    return true;
}

bool BrickPlayerGameInstance::OnKey(SDL_Event& event)
{
    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_LEFT:
            if (!isFreeBuildMode_)
                StepBackward();
            break;
        case SDLK_RIGHT:
            if (!isFreeBuildMode_)
                StepForward();
            break;
        case SDLK_SPACE:
        case SDLK_D:
            DisassembleSelected();
            break;
        case SDLK_R:
            ResetAll();
            break;
        case SDLK_N:
            if (isFreeBuildMode_)
                SpawnRandomBricks(6);
            break;
        case SDLK_Q:
            RotateDraggedPart90();
            break;
        default:
            break;
        }
    }
    return true;
}

bool BrickPlayerGameInstance::SupportsAppDebugShortcut(SDL_Keycode key) const
{
    switch (key)
    {
    case SDLK_F7:
    case SDLK_F8:
    case SDLK_F9:
        return true;
    default:
        return false;
    }
}

bool BrickPlayerGameInstance::IsAppDebugShortcutActive(SDL_Keycode key) const
{
    switch (key)
    {
    case SDLK_F7:
        return showSnapDebug_;
    case SDLK_F8:
        return GetEngine().GetShowFlags().DebugDraw_PhysicsBodies;
    case SDLK_F9:
        return useHorizontalDragPlane_;
    default:
        return false;
    }
}

bool BrickPlayerGameInstance::SetAppDebugShortcutActive(SDL_Keycode key, bool active)
{
    switch (key)
    {
    case SDLK_F7:
        showSnapDebug_ = active;
        return true;
    case SDLK_F8:
        GetEngine().GetShowFlags().DebugDraw_PhysicsBodies = active;
        return true;
    case SDLK_F9:
        if (useHorizontalDragPlane_ != active)
        {
            useHorizontalDragPlane_ = active;
            SwitchDragPlaneWhileDragging();
        }
        return true;
    default:
        return false;
    }
}

bool BrickPlayerGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (resetMouse_)
    {
        mousePos_ = glm::dvec2(xpos, ypos);
        resetMouse_ = false;
    }

    glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;

    if (isOrbitDragging_ && mouseLeftDown_ && !isDraggingPart_ && !mouseCapturedByUI_)
    {
        cameraRotX_ += static_cast<float>(delta.x) * cameraMultiplier_;
        cameraRotY_ += static_cast<float>(delta.y) * cameraMultiplier_;
        cameraRotY_ = std::clamp(cameraRotY_, -89.0f, 89.0f);
    }

    if (mouseRightDown_ && !mouseCapturedByUI_)
    {
        float panSpeed = cameraMultiplier_ * cameraArm_ * 0.002f;
        glm::vec3 panDelta = panForward_ * static_cast<float>(delta.y) * panSpeed;
        panDelta += panLeft_ * static_cast<float>(delta.x) * panSpeed;
        cameraCenter_ += panDelta;
        realCameraCenter_ += panDelta;
    }

    mousePos_ = glm::dvec2(xpos, ypos);
    return true;
}

bool BrickPlayerGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        mouseLeftDown_ = true;
        if (mouseCapturedByUI_)
        {
            isOrbitDragging_ = false;
            cameraMultiplier_ = 0.0f;
            return true;
        }

        if (!mouseCapturedByUI_ && StartDraggingHoveredPart())
        {
            isOrbitDragging_ = false;
            cameraMultiplier_ = 0.0f;
            return true;
        }

        isOrbitDragging_ = true;
        cameraMultiplier_ = 0.1f;
        return true;
    }
    else if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (isDraggingPart_)
        {
            StopDraggingPart();
        }
        else if (!isOrbitDragging_ || cameraMultiplier_ < 0.001f)
        {
            // Was a click, not a drag - handled by raycast
        }
        mouseLeftDown_ = false;
        isOrbitDragging_ = false;
        return true;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (mouseCapturedByUI_)
        {
            mouseRightDown_ = false;
            cameraMultiplier_ = 0.0f;
            return true;
        }

        mouseRightDown_ = true;
        cameraMultiplier_ = 0.1f;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        mouseRightDown_ = false;
        cameraMultiplier_ = 0.0f;
    }
    return true;
}

bool BrickPlayerGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (mouseCapturedByUI_)
    {
        return true;
    }

    const float scrollSpeed = 0.5f;
    cameraArm_ -= static_cast<float>(yoffset) * scrollSpeed;
    cameraArm_ = std::clamp(cameraArm_, 0.1f, 50.0f);
    return true;
}

void BrickPlayerGameInstance::OnRayHitResponse(Assets::RayCastResult& result)
{
    UpdateHitStateFromRaycast(result);
}

void BrickPlayerGameInstance::PerformRaycast()
{
    if (mouseCapturedByUI_)
    {
        ClearHoverTargets();
        return;
    }

    glm::vec3 rayOrigin;
    glm::vec3 rayDir;
    Runtime::EngineHelper::GetScreenToWorldRay(mousePos_, rayOrigin, rayDir);
    bool handled = false;
    GetEngine().RayCast(rayOrigin, rayDir, [this, &handled](Assets::RayCastResult result)
    {
        handled = this->UpdateHitStateFromRaycast(result);
        return true;
    });

    if (!handled)
    {
        ClearHoverTargets();
    }
}

bool BrickPlayerGameInstance::UpdateHitStateFromRaycast(const Assets::RayCastResult& result)
{
    if (!result.Hit)
    {
        return false;
    }

    const uint32_t instanceId = result.InstanceId;
    if (nodeStepMap_.find(instanceId) == nodeStepMap_.end())
    {
        return false;
    }

    const glm::vec3 hitNormal =
        BrickPlayer::Snap::NormalizeOrDefault(glm::vec3(result.Normal), glm::vec3(0.0f, 1.0f, 0.0f));

    if (isDraggingPart_)
    {
        if (instanceId == draggedInstanceId_ || disassembledNodes_.count(instanceId))
        {
            return false;
        }

        hoveredDisassembled_.instanceId = UINT32_MAX;
        hoveredAssembly_.instanceId = instanceId;
        hoveredAssembly_.hitPoint = glm::vec3(result.HitPoint);
        hoveredAssembly_.hitNormal = hitNormal;
        GetEngine().GetScene().SetHoveredId(instanceId);
        return true;
    }

    if (disassembledNodes_.count(instanceId))
    {
        hoveredDisassembled_.instanceId = instanceId;
        hoveredAssembly_.instanceId = UINT32_MAX;
        hoveredDisassembled_.hitPoint = glm::vec3(result.HitPoint);
        hoveredDisassembled_.hitNormal = hitNormal;
        // Clear any assembly hover selection so only one outline is visible at a time
        if (selectedInstanceId_ != UINT32_MAX)
        {
            selectedInstanceId_ = UINT32_MAX;
            GetEngine().GetScene().ClearSelection();
        }
        GetEngine().GetScene().SetHoveredId(instanceId);
        return true;
    }

    hoveredDisassembled_.instanceId = UINT32_MAX;
    hoveredAssembly_.instanceId = UINT32_MAX;
    GetEngine().GetScene().ClearHoveredId();
    selectedInstanceId_ = instanceId;
    selectedHitNormal_ = hitNormal;
    GetEngine().GetScene().SetSelectedId(instanceId);
    return true;
}

bool BrickPlayerGameInstance::StartDraggingHoveredPart()
{
    if (hoveredDisassembled_.instanceId == UINT32_MAX)
        return false;

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(hoveredDisassembled_.instanceId);
    auto [physComp, body] = GetPhysicsBody(node);
    if (!body)
        return false;

    draggedInstanceId_ = hoveredDisassembled_.instanceId;
    dragPlanePoint_ = hoveredDisassembled_.hitPoint;
    if (useHorizontalDragPlane_)
    {
        dragPlaneNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        dragPlaneNormal_ = BrickPlayer::Snap::NormalizeOrDefault(
            realCameraCenter_ - cachedCameraPos_, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    dragReleaseLinearVelocity_ = glm::vec3(0.0f);
    lastDraggedBodyPosition_ = body->position;
    hasDraggedBodyPositionSample_ = true;
    lockedDraggedConnectorIndex_ = FindDraggedConnectorLock(draggedInstanceId_, hoveredDisassembled_.hitPoint, hoveredDisassembled_.hitNormal);

    // Compute dragBodyOffset_ using the same CPU ray-plane intersection that
    // UpdateDraggedPart uses, so the first drag frame produces no positional jump.
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f};
    Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);
    glm::vec3 initialPlaneHit{0.0f};
    if (IntersectDragPlane(rayOrigin, rayDir, initialPlaneHit))
    {
        dragBodyOffset_ = body->position - initialPlaneHit;
    }
    else
    {
        dragBodyOffset_ = body->position - hoveredDisassembled_.hitPoint;
    }
    isDraggingPart_ = true;
    hoveredAssembly_.instanceId = UINT32_MAX;
    activeSnapCandidate_ = {};
    selectedInstanceId_ = UINT32_MAX;
    selectedHitNormal_ = hoveredDisassembled_.hitNormal;
    SetDraggedPartRayCastVisible(false);
    // Use hovered (green) outline for dragged part to distinguish from assembly hover (orange)
    GetEngine().GetScene().ClearSelection();
    GetEngine().GetScene().SetHoveredId(draggedInstanceId_);
    return true;
}

void BrickPlayerGameInstance::StopDraggingPart()
{
    if (!isDraggingPart_)
    {
        return;
    }

    const uint32_t finishedInstanceId = draggedInstanceId_;
    bool snappedBack = false;
    if (activeSnapCandidate_.valid)
    {
        snappedBack = ReattachDraggedPart();
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(finishedInstanceId);
    auto [physComp, body] = GetPhysicsBody(node);
    if (!snappedBack && body)
    {
        auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
        physics->SetBodyTransform(physComp->GetPhysicsBody(), body->position, body->rotation, true);
        glm::vec3 releaseVelocity = dragReleaseLinearVelocity_ * 0.82f;
        const float maxReleaseSpeed = 6.5f;
        const float releaseSpeed = glm::length(releaseVelocity);
        if (releaseSpeed > maxReleaseSpeed)
        {
            releaseVelocity *= maxReleaseSpeed / releaseSpeed;
        }
        if (glm::dot(releaseVelocity, releaseVelocity) > 0.01f * 0.01f)
        {
            physics->SetBodyVelocity(physComp->GetPhysicsBody(), releaseVelocity, glm::vec3(0.0f));
        }

        SetDraggedPartRayCastVisible(true);
        hoveredDisassembled_.instanceId = finishedInstanceId;
        GetEngine().GetScene().SetHoveredId(finishedInstanceId);
    }
    else if (snappedBack)
    {
        hoveredDisassembled_.instanceId = UINT32_MAX;
        GetEngine().GetScene().ClearHoveredId();
    }

    hoveredAssembly_.instanceId = UINT32_MAX;
    ResetDragState();
}

void BrickPlayerGameInstance::SwitchDragPlaneWhileDragging()
{
    if (!isDraggingPart_ || draggedInstanceId_ == UINT32_MAX)
        return;

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
    auto [physComp, body] = GetPhysicsBody(node);
    if (!body)
        return;

    // Switch the plane normal
    if (useHorizontalDragPlane_)
    {
        dragPlaneNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        dragPlaneNormal_ = BrickPlayer::Snap::NormalizeOrDefault(
            realCameraCenter_ - cachedCameraPos_, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Reposition the plane to pass through the current body position so there's no jump
    dragPlanePoint_ = body->position;

    // Recompute dragBodyOffset_ with the new plane
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f};
    Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);
    glm::vec3 planeHit{0.0f};
    if (IntersectDragPlane(rayOrigin, rayDir, planeHit))
    {
        dragBodyOffset_ = body->position - planeHit;
    }
}

void BrickPlayerGameInstance::RotateDraggedPart90()
{
    if (!isDraggingPart_ || draggedInstanceId_ == UINT32_MAX)
        return;

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
    auto [physComp, body] = GetPhysicsBody(node);
    if (!body)
        return;

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();

    // Rotate 90 degrees around world Y axis
    const glm::quat rotation90 = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat newRotation = glm::normalize(rotation90 * body->rotation);

    physics->SetBodyTransform(physComp->GetPhysicsBody(), body->position, newRotation, true);
    ApplyPhysicsPoseToNode(node, body->position, newRotation);

    // Clear snap state so it re-evaluates with the new rotation
    activeSnapCandidate_ = {};
}

void BrickPlayerGameInstance::UpdateDraggedPart()
{
    if (!isDraggingPart_ || draggedInstanceId_ == UINT32_MAX)
    {
        return;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
    auto [physComp, body] = GetPhysicsBody(node);
    if (!body)
    {
        StopDraggingPart();
        return;
    }

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();

    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f};
    Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);

    glm::vec3 planeHitPoint{0.0f};
    if (!IntersectDragPlane(rayOrigin, rayDir, planeHitPoint))
    {
        return;
    }

    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * node->Scale();
    const BrickPlayer::Shadow::FSnapConnector* lockedDraggedConnector = GetLockedDraggedConnector(draggedInstanceId_);
    // Always use dragBodyOffset_ for free dragging so the part follows the mouse
    // without any jump. Connector-based offsets are only used in the snap case below.
    glm::vec3 freeBodyPosition = planeHitPoint + dragBodyOffset_;

    if (hoveredAssembly_.instanceId != UINT32_MAX)
    {
        const glm::vec3 hoverNormal =
            BrickPlayer::Snap::NormalizeOrDefault(hoveredAssembly_.hitNormal, glm::vec3(0.0f, 1.0f, 0.0f));
        const float lduToWorldScale = GetLduToWorldScale();
        const float surfaceGap = std::max(8.0f * lduToWorldScale * 0.15f, 0.002f);
        if (lockedDraggedConnector)
        {
            const glm::vec3 connectorAnchor = hoveredAssembly_.hitPoint + hoverNormal * surfaceGap;
            freeBodyPosition = connectorAnchor - body->rotation * lockedDraggedConnector->localPosition + body->rotation * scaledOffset;
        }
        else
        {
            const auto disassembledIt = disassembledNodes_.find(draggedInstanceId_);
            const glm::vec3 halfExtent = disassembledIt != disassembledNodes_.end()
                ? disassembledIt->second.halfExtent
                : glm::vec3(0.01f);
            const float supportDistance = ProjectHalfExtentOnAxis(halfExtent, body->rotation, hoverNormal);
            freeBodyPosition = hoveredAssembly_.hitPoint + hoverNormal * (supportDistance + surfaceGap);
        }
    }
    else
    {
        float minBodyY = 0.0f;
        if (GetDraggedBodyMinimumY(node, body->rotation, minBodyY) && freeBodyPosition.y < minBodyY)
        {
            glm::vec3 floorAnchorPoint{0.0f};
            const float anchorPlaneY = minBodyY - dragBodyOffset_.y;
            if (IntersectHorizontalPlane(rayOrigin, rayDir, anchorPlaneY, floorAnchorPoint))
            {
                freeBodyPosition = floorAnchorPoint + dragBodyOffset_;
            }
            freeBodyPosition.y = minBodyY;
        }
    }
    ClampDraggedBodyPositionAboveFloor(node, body->rotation, freeBodyPosition);

    glm::vec3 desiredBodyPosition = freeBodyPosition;
    glm::quat desiredBodyRotation = body->rotation;
    const DragSnapCandidate previousSnapCandidate = activeSnapCandidate_;

    DragSnapCandidate snapCandidate;
    if (TryBuildSnapCandidate(node, physComp, freeBodyPosition, snapCandidate))
    {
        activeSnapCandidate_ = snapCandidate;
        const bool snapChanged = !previousSnapCandidate.valid
            || previousSnapCandidate.restoreOriginalHierarchy != snapCandidate.restoreOriginalHierarchy
            || previousSnapCandidate.targetInstanceId != snapCandidate.targetInstanceId
            || previousSnapCandidate.draggedConnectorIndex != snapCandidate.draggedConnectorIndex
            || previousSnapCandidate.targetConnectorIndex != snapCandidate.targetConnectorIndex
            || glm::abs(glm::dot(previousSnapCandidate.desiredRotation, snapCandidate.desiredRotation)) < 0.9995f;
        const bool snapAudioChanged = !previousSnapCandidate.valid
            || previousSnapCandidate.restoreOriginalHierarchy != snapCandidate.restoreOriginalHierarchy
            || previousSnapCandidate.targetInstanceId != snapCandidate.targetInstanceId
            || previousSnapCandidate.draggedConnectorIndex != snapCandidate.draggedConnectorIndex
            || previousSnapCandidate.targetConnectorIndex != snapCandidate.targetConnectorIndex;
        if (snapChanged)
        {
            snapFeedbackPulseUntil_ = GetEngine().GetTime() + 0.12f;
        }
        if (snapAudioChanged)
        {
            PlayRandomPutSound();
        }
        desiredBodyPosition = snapCandidate.desiredBodyPosition;
        desiredBodyRotation = snapCandidate.desiredRotation;
        hoveredDisassembled_.hitPoint = snapCandidate.desiredTranslation;
    }
    else
    {
        activeSnapCandidate_ = {};
        hoveredDisassembled_.hitPoint = hoveredAssembly_.instanceId != UINT32_MAX ? hoveredAssembly_.hitPoint : planeHitPoint;
    }

    const float deltaSeconds = std::max(static_cast<float>(GetEngine().GetDeltaSeconds()), 1.0f / 240.0f);
    if (hasDraggedBodyPositionSample_)
    {
        glm::vec3 frameVelocity = (desiredBodyPosition - lastDraggedBodyPosition_) / deltaSeconds;
        const float maxTrackedSpeed = 10.0f;
        const float trackedSpeed = glm::length(frameVelocity);
        if (trackedSpeed > maxTrackedSpeed)
        {
            frameVelocity *= maxTrackedSpeed / trackedSpeed;
        }

        const glm::vec3 targetVelocity = activeSnapCandidate_.valid ? glm::vec3(0.0f) : frameVelocity;
        dragReleaseLinearVelocity_ = glm::mix(dragReleaseLinearVelocity_, targetVelocity, 0.35f);
    }
    else
    {
        hasDraggedBodyPositionSample_ = true;
    }
    lastDraggedBodyPosition_ = desiredBodyPosition;

    physics->SetBodyTransform(physComp->GetPhysicsBody(), desiredBodyPosition, desiredBodyRotation, true);
    ApplyPhysicsPoseToNode(node, desiredBodyPosition, desiredBodyRotation);

    hoveredDisassembled_.instanceId = draggedInstanceId_;
    // Use hovered (green) outline for dragged part to distinguish from assembly hover (orange)
    GetEngine().GetScene().SetHoveredId(draggedInstanceId_);
    GetEngine().GetScene().ClearSelection();
    selectedInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().MarkDirty();
}

bool BrickPlayerGameInstance::TryBuildSnapCandidate(Assets::Node* node,
                                                    const Runtime::PhysicsComponent* physComp,
                                                    const glm::vec3& freeBodyPosition,
                                                    DragSnapCandidate& outCandidate)
{
    outCandidate = {};

    if (TryBuildShadowSnapCandidate(node, physComp, freeBodyPosition, outCandidate))
    {
        return true;
    }

    return TryBuildOriginalSnapCandidate(physComp, freeBodyPosition, outCandidate);
}

bool BrickPlayerGameInstance::TryBuildShadowSnapCandidate(Assets::Node* node,
                                                          const Runtime::PhysicsComponent* physComp,
                                                          const glm::vec3& freeBodyPosition,
                                                          DragSnapCandidate& outCandidate)
{
    outCandidate = {};

    if (!node || !physComp || hoveredAssembly_.instanceId == UINT32_MAX)
    {
        return false;
    }

    auto partIt = nodePartFileMap_.find(draggedInstanceId_);
    if (partIt == nodePartFileMap_.end())
    {
        return false;
    }

    const auto& draggedConnectors = shadowLibrary_.GetConnectorsForPart(partIt->second);
    const std::vector<WorldSnapConnector> targetConnectors = BuildWorldConnectors(hoveredAssembly_.instanceId);
    if (draggedConnectors.empty() || targetConnectors.empty())
    {
        return false;
    }

    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * node->Scale();
    const float snapThreshold = GetSnapDistanceThreshold(draggedInstanceId_);
    const float lduToWorldScale = GetLduToWorldScale();
    const float twistAngles[] = {
        0.0f,
        glm::half_pi<float>(),
        glm::pi<float>(),
        glm::half_pi<float>() * 3.0f
    };
    const glm::quat currentDragRotation = node->WorldRotation();

    bool foundCandidate = false;
    int bestScore = -1;
    float bestDistance = FLT_MAX;
    bool bestMatchesActiveCandidate = false;
    const bool hasLockedConnector = lockedDraggedConnectorIndex_ >= 0
        && static_cast<size_t>(lockedDraggedConnectorIndex_) < draggedConnectors.size();

    auto evaluateCandidates = [&](bool lockedOnly) -> bool
    {
        const bool foundBeforePass = foundCandidate;
        for (size_t draggedConnectorIndex = 0; draggedConnectorIndex < draggedConnectors.size(); ++draggedConnectorIndex)
        {
            const bool isLockedConnector = static_cast<int32_t>(draggedConnectorIndex) == lockedDraggedConnectorIndex_;
            if (lockedOnly && hasLockedConnector && !isLockedConnector)
            {
                continue;
            }
            if (!lockedOnly && hasLockedConnector && isLockedConnector)
            {
                continue;
            }

            const auto& draggedConnector = draggedConnectors[draggedConnectorIndex];
            for (size_t targetConnectorIndex = 0; targetConnectorIndex < targetConnectors.size(); ++targetConnectorIndex)
            {
                const WorldSnapConnector& targetConnector = targetConnectors[targetConnectorIndex];
                if (!targetConnector.connector
                    || !BrickPlayer::Snap::AreConnectorsCompatible(draggedConnector, *targetConnector.connector, lduToWorldScale))
                {
                    continue;
                }

                const BrickPlayer::Snap::FHoverFilterResult hoverFilter =
                    BrickPlayer::Snap::EvaluateHoverFilter(*targetConnector.connector,
                                                           targetConnector.worldPosition,
                                                           targetConnector.worldAxis,
                                                           hoveredAssembly_.hitPoint,
                                                           hoveredAssembly_.hitNormal,
                                                           lduToWorldScale);
                if (!hoverFilter.passes)
                {
                    continue;
                }

                const glm::quat baseRotation =
                    glm::normalize(targetConnector.worldRotation * glm::inverse(draggedConnector.localRotation));

                for (float twistAngle : twistAngles)
                {
                    const glm::quat twist = glm::angleAxis(twistAngle, targetConnector.worldAxis);
                    const glm::quat desiredRotation = glm::normalize(twist * baseRotation);
                    const glm::vec3 desiredTranslation = targetConnector.worldPosition - desiredRotation * draggedConnector.localPosition;
                    const glm::vec3 desiredBodyPosition = desiredTranslation + desiredRotation * scaledOffset;

                    // When the locked connector differs from the tested one, freeBodyPosition
                    // was anchored to the locked connector's surface contact.  Re-anchor it to
                    // the tested connector so the distance comparison reflects the actual
                    // proximity (e.g. picking up a brick by its top stud but snapping via
                    // bottom anti-studs would otherwise produce a full-brick-height offset).
                    glm::vec3 adjustedFreeBodyPosition = freeBodyPosition;
                    if (hasLockedConnector && !isLockedConnector)
                    {
                        const glm::vec3 connectorDelta =
                            currentDragRotation * (draggedConnector.localPosition
                                                   - draggedConnectors[lockedDraggedConnectorIndex_].localPosition);
                        adjustedFreeBodyPosition -= connectorDelta;
                    }

                    const float distanceToSnap = glm::distance(adjustedFreeBodyPosition, desiredBodyPosition);
                    if (distanceToSnap > snapThreshold)
                    {
                        continue;
                    }

                    const int hoverScore = static_cast<int>(std::round(
                        (hoverFilter.hoverDistanceLimit - hoverFilter.hoverDistance) * 100.0f));
                    const int depthScore = static_cast<int>(std::round(
                        (hoverFilter.hoverDepthLimit - glm::abs(hoverFilter.hoverDepth)) * 100.0f));
                    const int normalScore = static_cast<int>(std::round(
                        glm::dot(targetConnector.worldAxis, hoverFilter.hoverNormal) * 1000.0f));
                    const int lockScore = isLockedConnector ? 5000 : 0;
                    const int rotationScore =
                        static_cast<int>(std::round(glm::abs(glm::dot(desiredRotation, currentDragRotation)) * 4000.0f));
                    const bool matchesActiveCandidate = activeSnapCandidate_.valid
                        && !activeSnapCandidate_.restoreOriginalHierarchy
                        && activeSnapCandidate_.targetInstanceId == targetConnector.ownerInstanceId
                        && activeSnapCandidate_.draggedConnectorIndex == static_cast<int32_t>(draggedConnectorIndex)
                        && activeSnapCandidate_.targetConnectorIndex == static_cast<int32_t>(targetConnectorIndex);
                    const int stickyScore = matchesActiveCandidate ? 2000 : 0;
                    const int score =
                        normalScore + depthScore + hoverScore + lockScore + rotationScore + stickyScore;

                    const int replaceMargin = bestMatchesActiveCandidate && !matchesActiveCandidate ? 800 : 0;
                    const bool isBetter = !foundCandidate
                        || score > bestScore + replaceMargin
                        || (score == bestScore && matchesActiveCandidate && !bestMatchesActiveCandidate)
                        || (score == bestScore && matchesActiveCandidate == bestMatchesActiveCandidate && distanceToSnap < bestDistance);
                    if (!isBetter)
                    {
                        continue;
                    }

                    foundCandidate = true;
                    bestScore = score;
                    bestDistance = distanceToSnap;
                    bestMatchesActiveCandidate = matchesActiveCandidate;

                    outCandidate.valid = true;
                    outCandidate.targetInstanceId = targetConnector.ownerInstanceId;
                    outCandidate.desiredTranslation = desiredTranslation;
                    outCandidate.desiredRotation = desiredRotation;
                    outCandidate.desiredBodyPosition = desiredBodyPosition;
                    outCandidate.restoreOriginalHierarchy = false;
                    outCandidate.draggedConnectorIndex = static_cast<int32_t>(draggedConnectorIndex);
                    outCandidate.targetConnectorIndex = static_cast<int32_t>(targetConnectorIndex);
                }
            }
        }

        return foundCandidate && !foundBeforePass;
    };

    if (hasLockedConnector)
    {
        evaluateCandidates(true);
        if (foundCandidate)
        {
            return true;
        }
    }

    evaluateCandidates(false);

    return foundCandidate;
}

bool BrickPlayerGameInstance::TryBuildOriginalSnapCandidate(const Runtime::PhysicsComponent* physComp,
                                                            const glm::vec3& freeBodyPosition,
                                                            DragSnapCandidate& outCandidate)
{
    outCandidate = {};

    if (!physComp || hoveredAssembly_.instanceId == UINT32_MAX)
    {
        return false;
    }

    if (!AreNodesOriginallyConnectable(draggedInstanceId_, hoveredAssembly_.instanceId))
    {
        return false;
    }

    auto originalIt = originalAssemblyStates_.find(draggedInstanceId_);
    if (originalIt == originalAssemblyStates_.end())
    {
        return false;
    }

    const OriginalAssemblyState& originalState = originalIt->second;
    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * originalState.worldScale;
    const glm::vec3 desiredBodyPosition = originalState.worldTranslation + originalState.worldRotation * scaledOffset;
    const float distanceToSnap = glm::distance(freeBodyPosition, desiredBodyPosition);
    if (distanceToSnap > GetSnapDistanceThreshold(draggedInstanceId_))
    {
        return false;
    }

    outCandidate.valid = true;
    outCandidate.targetInstanceId = hoveredAssembly_.instanceId;
    outCandidate.desiredTranslation = originalState.worldTranslation;
    outCandidate.desiredRotation = originalState.worldRotation;
    outCandidate.desiredBodyPosition = desiredBodyPosition;
    outCandidate.restoreOriginalHierarchy = true;
    return true;
}

std::vector<BrickPlayerGameInstance::WorldSnapConnector> BrickPlayerGameInstance::BuildWorldConnectors(uint32_t instanceId) const
{
    std::vector<WorldSnapConnector> worldConnectors;

    auto partIt = nodePartFileMap_.find(instanceId);
    if (partIt == nodePartFileMap_.end())
    {
        return worldConnectors;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(instanceId);
    if (!node)
    {
        return worldConnectors;
    }

    const auto& localConnectors = shadowLibrary_.GetConnectorsForPart(partIt->second);
    if (localConnectors.empty())
    {
        return worldConnectors;
    }

    worldConnectors.reserve(localConnectors.size());
    const glm::mat4 worldTransform = node->WorldTransform();
    const glm::quat nodeRotation = node->WorldRotation();

    for (const BrickPlayer::Shadow::FSnapConnector& localConnector : localConnectors)
    {
        WorldSnapConnector worldConnector;
        worldConnector.connector = &localConnector;
        worldConnector.ownerInstanceId = instanceId;
        worldConnector.worldPosition = glm::vec3(worldTransform * glm::vec4(localConnector.localPosition, 1.0f));
        worldConnector.worldRotation = glm::normalize(nodeRotation * localConnector.localRotation);
        worldConnector.worldAxis = glm::normalize(worldConnector.worldRotation * glm::vec3(0.0f, 1.0f, 0.0f));
        worldConnectors.push_back(std::move(worldConnector));
    }

    return worldConnectors;
}

int32_t BrickPlayerGameInstance::FindDraggedConnectorLock(uint32_t instanceId,
                                                          const glm::vec3& hitPoint,
                                                          const glm::vec3& hitNormal) const
{
    const std::vector<WorldSnapConnector> worldConnectors = BuildWorldConnectors(instanceId);
    if (worldConnectors.empty())
    {
        return -1;
    }

    const glm::vec3 normalizedHitNormal =
        BrickPlayer::Snap::NormalizeOrDefault(hitNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const BrickPlayer::Snap::FScaleMetrics scaleMetrics = BrickPlayer::Snap::BuildScaleMetrics(GetLduToWorldScale());

    float bestScore = FLT_MAX;
    int32_t bestIndex = -1;

    for (size_t connectorIndex = 0; connectorIndex < worldConnectors.size(); ++connectorIndex)
    {
        const WorldSnapConnector& worldConnector = worldConnectors[connectorIndex];
        if (!worldConnector.connector)
        {
            continue;
        }

        const glm::vec3 hitToConnector = worldConnector.worldPosition - hitPoint;
        const float depth = glm::abs(glm::dot(hitToConnector, normalizedHitNormal));
        const glm::vec3 planarOffset = hitToConnector - normalizedHitNormal * glm::dot(hitToConnector, normalizedHitNormal);
        const float planarDistance = glm::length(planarOffset);
        const float normalPenalty = 1.0f - glm::abs(glm::dot(worldConnector.worldAxis, normalizedHitNormal));
        const float score = planarDistance + depth * 0.35f + normalPenalty * scaleMetrics.studPitch * 0.5f;
        const float scoreLimit = std::max(scaleMetrics.studPitch * 1.25f,
                                          worldConnector.connector->radius * 2.5f + worldConnector.connector->length * 0.5f);
        if (score > scoreLimit || score >= bestScore)
        {
            continue;
        }

        bestScore = score;
        bestIndex = static_cast<int32_t>(connectorIndex);
    }

    return bestIndex;
}

const BrickPlayer::Shadow::FSnapConnector* BrickPlayerGameInstance::GetLockedDraggedConnector(uint32_t instanceId) const
{
    if (lockedDraggedConnectorIndex_ < 0)
    {
        return nullptr;
    }

    auto partIt = nodePartFileMap_.find(instanceId);
    if (partIt == nodePartFileMap_.end())
    {
        return nullptr;
    }

    const auto& localConnectors = shadowLibrary_.GetConnectorsForPart(partIt->second);
    if (static_cast<size_t>(lockedDraggedConnectorIndex_) >= localConnectors.size())
    {
        return nullptr;
    }

    return &localConnectors[lockedDraggedConnectorIndex_];
}

bool BrickPlayerGameInstance::AreNodesOriginallyConnectable(uint32_t draggedId, uint32_t targetId) const
{
    const auto draggedIt = originalAssemblyStates_.find(draggedId);
    const auto targetIt = originalAssemblyStates_.find(targetId);
    if (draggedIt == originalAssemblyStates_.end() || targetIt == originalAssemblyStates_.end())
    {
        return false;
    }

    const BrickPlayer::Snap::FScaleMetrics scaleMetrics =
        BrickPlayer::Snap::BuildScaleMetrics(GetLduToWorldScale());
    const float connectMargin = std::max(scaleMetrics.studPitch * 0.35f, scaleMetrics.plateHeight * 1.5f);

    const OriginalAssemblyState& draggedState = draggedIt->second;
    const OriginalAssemblyState& targetState = targetIt->second;

    const float gapX = AxisGap(
        draggedState.worldAabbMin.x, draggedState.worldAabbMax.x,
        targetState.worldAabbMin.x, targetState.worldAabbMax.x);
    const float gapY = AxisGap(
        draggedState.worldAabbMin.y, draggedState.worldAabbMax.y,
        targetState.worldAabbMin.y, targetState.worldAabbMax.y);
    const float gapZ = AxisGap(
        draggedState.worldAabbMin.z, draggedState.worldAabbMax.z,
        targetState.worldAabbMin.z, targetState.worldAabbMax.z);

    return gapX <= connectMargin && gapY <= connectMargin && gapZ <= connectMargin;
}

float BrickPlayerGameInstance::GetSnapDistanceThreshold(uint32_t draggedId) const
{
    const BrickPlayer::Snap::FScaleMetrics scaleMetrics =
        BrickPlayer::Snap::BuildScaleMetrics(GetLduToWorldScale());

    float sizeBasedThreshold = 0.0f;
    auto it = disassembledNodes_.find(draggedId);
    if (it != disassembledNodes_.end())
    {
        const glm::vec3 halfExtent = it->second.halfExtent;
        sizeBasedThreshold = std::max(halfExtent.x, std::max(halfExtent.y, halfExtent.z)) * 0.5f;
    }

    return std::max(std::max(scaleMetrics.studPitch * 1.25f, scaleMetrics.plateHeight * 8.0f), sizeBasedThreshold);
}

void BrickPlayerGameInstance::SetDraggedPartRayCastVisible(bool visible)
{
    if (draggedInstanceId_ == UINT32_MAX)
    {
        return;
    }

    auto node = GetEngine().GetScene().GetNodeSharedByInstanceId(draggedInstanceId_);
    if (!node)
    {
        return;
    }

    auto render = node->GetComponent<Runtime::RenderComponent>();
    if (!render || render->GetRayCastVisible() == visible)
    {
        return;
    }

    Assets::NodeUtils::SetRayCastVisible(node, visible);
    GetEngine().GetScene().MarkDirty();
}

bool BrickPlayerGameInstance::ReattachDraggedPart()
{
    if (draggedInstanceId_ == UINT32_MAX || !activeSnapCandidate_.valid)
    {
        return false;
    }

    auto originalIt = originalAssemblyStates_.find(draggedInstanceId_);
    auto node = GetEngine().GetScene().GetNodeSharedByInstanceId(draggedInstanceId_);
    if (originalIt == originalAssemblyStates_.end() || !node)
    {
        return false;
    }

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    if (physComp && physics)
    {
        const NextBodyID bodyId = physComp->GetPhysicsBody();
        if (!bodyId.IsInvalid())
        {
            physics->RemoveBody(bodyId);
        }
    }
    node->AddComponent(std::shared_ptr<Runtime::PhysicsComponent>{});

    if (activeSnapCandidate_.restoreOriginalHierarchy)
    {
        const OriginalAssemblyState& originalState = originalIt->second;
        if (originalState.parentInstanceId != UINT32_MAX)
        {
            auto parent = GetEngine().GetScene().GetNodeSharedByInstanceId(originalState.parentInstanceId);
            if (parent)
            {
                node->SetParent(parent);
                node->SetTranslation(originalState.localTranslation);
                node->SetRotation(originalState.localRotation);
                node->SetScale(originalState.localScale);
            }
            else
            {
                node->ClearParent();
                node->SetTranslation(originalState.worldTranslation);
                node->SetRotation(originalState.worldRotation);
                node->SetScale(originalState.worldScale);
            }
        }
        else
        {
            node->ClearParent();
            node->SetTranslation(originalState.localTranslation);
            node->SetRotation(originalState.localRotation);
            node->SetScale(originalState.localScale);
        }
    }
    else
    {
        node->ClearParent();
        node->SetTranslation(activeSnapCandidate_.desiredTranslation);
        node->SetRotation(activeSnapCandidate_.desiredRotation);
    }

    node->RecalcTransform(true);
    GetEngine().GetScene().EnsureNodePhysicsBody(node.get());
    SetDraggedPartRayCastVisible(true);

    disassembledNodes_.erase(draggedInstanceId_);
    selectedInstanceId_ = draggedInstanceId_;
    GetEngine().GetScene().SetSelectedId(draggedInstanceId_);
    GetEngine().GetScene().MarkDirty();
    return true;
}

void BrickPlayerGameInstance::PlayRandomPutSound()
{
    GetEngine().GetAudio()->PlaySound(GetRandomPutSoundPath(), false, 0.55f);
}

bool BrickPlayerGameInstance::IntersectDragPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& outPoint) const
{
    const float denom = glm::dot(rayDir, dragPlaneNormal_);
    if (glm::abs(denom) < 1e-4f)
    {
        return false;
    }

    const float t = glm::dot(dragPlanePoint_ - rayOrigin, dragPlaneNormal_) / denom;
    if (t < 0.0f)
    {
        return false;
    }

    outPoint = rayOrigin + rayDir * t;
    return true;
}

void BrickPlayerGameInstance::ApplyPhysicsPoseToNode(Assets::Node* node, const glm::vec3& bodyPosition, const glm::quat& bodyRotation)
{
    if (!node)
    {
        return;
    }

    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    if (!physComp)
    {
        return;
    }

    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * node->Scale();
    const glm::vec3 newTranslation = bodyPosition - bodyRotation * scaledOffset;
    node->SetTranslation(newTranslation);
    node->SetRotation(bodyRotation);
    node->RecalcTransform(true);
}

bool BrickPlayerGameInstance::GetDraggedBodyMinimumY(Assets::Node* node,
                                                     const glm::quat& bodyRotation,
                                                     float& outMinBodyY) const
{
    if (!hasFloorPlane_)
    {
        return false;
    }

    if (!node)
    {
        return false;
    }

    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    auto render = node->GetComponent<Runtime::RenderComponent>();
    if (!physComp || !render || !render->IsDrawable())
    {
        return false;
    }

    const auto* model = GetEngine().GetScene().GetModel(render->GetModelId());
    if (!model)
    {
        return false;
    }

    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * node->Scale();
    const glm::mat4 bodyLocalTransform = glm::translate(glm::mat4(1.0f), -bodyRotation * scaledOffset)
        * glm::mat4_cast(bodyRotation)
        * glm::scale(glm::mat4(1.0f), node->Scale());
    const WorldBounds bounds = TransformLocalBounds(bodyLocalTransform, model->GetLocalAABBMin(), model->GetLocalAABBMax());
    outMinBodyY = floorSurfaceY_ - bounds.min.y;
    return true;
}

bool BrickPlayerGameInstance::ClampDraggedBodyPositionAboveFloor(Assets::Node* node,
                                                                 const glm::quat& bodyRotation,
                                                                 glm::vec3& inOutBodyPosition) const
{
    float minBodyY = 0.0f;
    if (!GetDraggedBodyMinimumY(node, bodyRotation, minBodyY) || inOutBodyPosition.y >= minBodyY)
    {
        return false;
    }

    inOutBodyPosition.y = minBodyY;
    return true;
}

// Timeline

void BrickPlayerGameInstance::SetCurrentStep(int32_t step)
{
    const int32_t maxStep = GetMaxTimelineStep();
    if (maxStep <= 0)
        return;
    const int32_t previousStep = currentStep_;
    currentStep_ = std::clamp(step, 0, maxStep - 1);
    const bool playPlacementSound = sceneLoaded_ && currentStep_ > previousStep;
    UpdateVisibilityForStep(currentStep_, playPlacementSound);
}

void BrickPlayerGameInstance::SetPerPartMode(bool enabled)
{
    if (perPartMode_ == enabled)
        return;
    perPartMode_ = enabled;
    autoPlay_ = false;

    // Reset to step 0 when switching modes
    currentStep_ = 0;
    UpdateVisibilityForStep(currentStep_, false);
}

void BrickPlayerGameInstance::ToggleAutoPlay()
{
    autoPlay_ = !autoPlay_;
    autoPlayTimer_ = 0.0f;

    // If starting auto-play and already at the end, restart from 0
    if (autoPlay_)
    {
        const int32_t maxStep = GetMaxTimelineStep();
        if (currentStep_ >= maxStep - 1)
        {
            currentStep_ = 0;
            UpdateVisibilityForStep(currentStep_, false);
        }
    }
}

void BrickPlayerGameInstance::CyclePlaySpeed()
{
    playSpeedIndex_ = (playSpeedIndex_ + 1) % playSpeedCount;
}

const char* BrickPlayerGameInstance::GetPlaySpeedLabel() const
{
    return playSpeedPresets[playSpeedIndex_].label;
}

void BrickPlayerGameInstance::PlayNextBGM()
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return;
    }

    if (!currentTrack->path.empty())
    {
        GetEngine().GetAudio()->PauseSound(currentTrack->path, true);
    }

    currentBGM_ = (currentBGM_ + 1) % static_cast<uint32_t>(bgmTracks_.size());
    currentTrack = GetCurrentBGMTrack();
    if (currentTrack && !currentTrack->path.empty())
    {
        GetEngine().GetAudio()->PlaySound(currentTrack->path, true, 0.45f);
    }
}

bool BrickPlayerGameInstance::IsBGMPaused() const
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return true;
    }

    return !GetEngine().GetAudio()->IsSoundPlaying(currentTrack->path);
}

void BrickPlayerGameInstance::PauseBGM(bool pause)
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return;
    }

    GetEngine().GetAudio()->PauseSound(currentTrack->path, pause);
}

std::string BrickPlayerGameInstance::GetCurrentBGMName() const
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return {};
    }

    return currentTrack->name;
}

void BrickPlayerGameInstance::BuildPerPartOrder()
{
    nodePartOrder_.clear();

    // Collect renderable nodes that are in the step map
    struct PartEntry
    {
        uint32_t instanceId;
        int32_t buildStep;
    };
    std::vector<PartEntry> entries;

    auto& scene = GetEngine().GetScene();
    for (auto* render : scene.Components<Runtime::RenderComponent>())
    {
        Assets::Node* node = render->GetOwner();
        if (!node)
            continue;
        uint32_t instanceId = node->GetInstanceId();
        auto it = nodeStepMap_.find(instanceId);
        if (it == nodeStepMap_.end())
            continue;

        if (!render->IsDrawable())
            continue;

        entries.push_back({instanceId, it->second});
    }

    // Sort by build step first (preserve building order), then by instance ID
    std::sort(entries.begin(), entries.end(), [](const PartEntry& a, const PartEntry& b)
    {
        if (a.buildStep != b.buildStep)
            return a.buildStep < b.buildStep;
        return a.instanceId < b.instanceId;
    });

    for (int32_t i = 0; i < static_cast<int32_t>(entries.size()); ++i)
    {
        nodePartOrder_[entries[i].instanceId] = i;
    }

    totalParts_ = static_cast<int32_t>(entries.size());
}

void BrickPlayerGameInstance::CaptureOriginalAssemblyState()
{
    originalAssemblyStates_.clear();

    auto& scene = GetEngine().GetScene();
    const auto& models = scene.Models();

    for (const auto* render : scene.Components<Runtime::RenderComponent>())
    {
        const Assets::Node* node = render->GetOwner();
        if (!node)
        {
            continue;
        }
        const uint32_t instanceId = node->GetInstanceId();
        if (nodeStepMap_.find(instanceId) == nodeStepMap_.end())
        {
            continue;
        }

        if (!render->IsDrawable())
        {
            continue;
        }

        const uint32_t modelId = render->GetModelId();
        if (modelId >= models.size())
        {
            continue;
        }

        const Assets::Model& model = models[modelId];
        const WorldBounds worldBounds = TransformLocalBounds(
            node->WorldTransform(),
            model.GetLocalAABBMin(),
            model.GetLocalAABBMax());

        OriginalAssemblyState state;
        state.parentInstanceId = node->GetParent() ? node->GetParent()->GetInstanceId() : UINT32_MAX;
        state.localTranslation = node->Translation();
        state.localRotation = node->Rotation();
        state.localScale = node->Scale();
        state.worldTranslation = node->WorldTranslation();
        state.worldRotation = node->WorldRotation();
        state.worldScale = node->WorldScale();
        state.worldAabbMin = worldBounds.min;
        state.worldAabbMax = worldBounds.max;

        originalAssemblyStates_[instanceId] = state;
    }
}

void BrickPlayerGameInstance::StepForward()
{
    SetCurrentStep(currentStep_ + 1);
}

void BrickPlayerGameInstance::StepBackward()
{
    SetCurrentStep(currentStep_ - 1);
}

void BrickPlayerGameInstance::UpdateVisibilityForStep(int32_t step, bool playPlacementSound)
{
    auto& scene = GetEngine().GetScene();
    bool changed = false;
    int32_t revealedCount = 0;
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();

    const auto& lookupMap = perPartMode_ ? nodePartOrder_ : nodeStepMap_;

    for (auto* render : scene.Components<Runtime::RenderComponent>())
    {
        Assets::Node* node = render->GetOwner();
        if (!node)
            continue;

        uint32_t instanceId = node->GetInstanceId();

        // Skip disassembled parts (they stay visible with physics)
        if (disassembledNodes_.count(instanceId))
            continue;

        auto it = lookupMap.find(instanceId);
        if (it != lookupMap.end())
        {
            bool shouldBeVisible = it->second <= step;
            const bool wasVisible = render->GetVisible();
            auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
            if (wasVisible != shouldBeVisible)
            {
                render->SetVisible(shouldBeVisible);
                render->SetRayCastVisible(shouldBeVisible);
                if (physComp && physics)
                {
                    const NextBodyID bodyId = physComp->GetPhysicsBody();
                    if (!bodyId.IsInvalid())
                    {
                        physics->SetBodyActive(bodyId, shouldBeVisible);
                    }
                }
                changed = true;
                if (playPlacementSound && shouldBeVisible)
                {
                    ++revealedCount;
                }
            }
            else if (physComp && physics)
            {
                const NextBodyID bodyId = physComp->GetPhysicsBody();
                if (!bodyId.IsInvalid())
                {
                    physics->SetBodyActive(bodyId, shouldBeVisible);
                }
            }
        }
    }

    if (changed)
    {
        GetEngine().GetScene().MarkDirty();
    }

    if (playPlacementSound && revealedCount > 0)
    {
        for (int32_t i = 0; i < revealedCount; ++i)
        {
            PlayRandomPutSound();
        }
    }
}

// Disassemble

void BrickPlayerGameInstance::DisassembleSelected()
{
    if (selectedInstanceId_ == UINT32_MAX)
        return;

    if (disassembledNodes_.count(selectedInstanceId_))
        return;

    // In FreeBuild mode, prevent disassembling baseplates (step 0)
    if (isFreeBuildMode_)
    {
        auto stepIt = nodeStepMap_.find(selectedInstanceId_);
        if (stepIt != nodeStepMap_.end() && stepIt->second <= 0)
            return;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(selectedInstanceId_);
    if (!node)
        return;

    // Record world transform before detaching from parent
    glm::vec3 worldTranslation = node->WorldTranslation();
    glm::quat worldRotation = node->WorldRotation();
    glm::vec3 worldScale = node->WorldScale();

    // Detach from parent so physics can write world-space directly to Translation()
    if (node->GetParent())
    {
        node->ClearParent();
        node->SetTranslation(worldTranslation);
        node->SetRotation(worldRotation);
        node->Scale() = worldScale;
        node->RecalcTransform(true);
    }

    auto bodyResult = CreateDynamicPhysicsBody(node, worldScale, worldRotation);
    if (!bodyResult.created)
        return;

    // Apply force along the hit surface normal so the part pops outward
    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (physComp && physics)
    {
        const NextBodyID bodyId = physComp->GetPhysicsBody();
        const auto* model = GetEngine().GetScene().GetModel(
            node->GetComponent<Runtime::RenderComponent>()->GetModelId());
        glm::vec3 fullExtent = glm::max((model->GetLocalAABBMax() - model->GetLocalAABBMin()) * glm::abs(worldScale), glm::vec3(0.002f));
        float volume = fullExtent.x * fullExtent.y * fullExtent.z;
        float impulseMagnitude = std::max(volume * 80000.0f, 800.0f);
        glm::vec3 forceDir = glm::length(selectedHitNormal_) > 0.001f
            ? glm::normalize(selectedHitNormal_)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        physics->AddForceToBody(bodyId, forceDir * impulseMagnitude);
    }

    disassembledNodes_[selectedInstanceId_] = {bodyResult.halfExtent};
    hoveredDisassembled_.instanceId = UINT32_MAX;
    selectedInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().ClearSelection();
    GetEngine().GetScene().ClearHoveredId();
    GetEngine().GetScene().MarkDirty();
}

void BrickPlayerGameInstance::ResetAll()
{
    if (!sceneLoaded_)
        return;

    // Reload the scene to restore all transforms and remove physics bodies
    sceneLoaded_ = false;
    ResetInteractiveSceneState();
    nodeStepMap_.clear();
    nodePartOrder_.clear();
    nodePartFileMap_.clear();

    // Clear FreeBuild state
    freeBuildInventory_.clear();
    isFreeBuildMode_ = false;

    // Re-request the same scene
    if (!currentScenePath_.empty())
        GetEngine().RequestLoadScene({.filename = currentScenePath_});
}

void BrickPlayerGameInstance::StartFreeBuild()
{
    currentScenePath_ = "assets/omr/freebuild.ldr";
    GetEngine().RequestLoadScene({.filename = currentScenePath_});
}

void BrickPlayerGameInstance::BuildFreeBuildInventory()
{
    freeBuildInventory_.clear();
    auto& scene = GetEngine().GetScene();

    for (auto* render : scene.Components<Runtime::RenderComponent>())
    {
        Assets::Node* node = render->GetOwner();
        if (!node)
            continue;
        uint32_t instanceId = node->GetInstanceId();

        // Only include inventory bricks (step > 0), skip baseplates (step 0)
        auto stepIt = nodeStepMap_.find(instanceId);
        if (stepIt == nodeStepMap_.end() || stepIt->second <= 0)
            continue;

        if (!render->IsDrawable())
            continue;

        auto partIt = nodePartFileMap_.find(instanceId);
        if (partIt == nodePartFileMap_.end())
            continue;

        InventoryTemplate tmpl;
        tmpl.sourceInstanceId = instanceId;
        tmpl.modelId = render->GetModelId();
        tmpl.materials = render->GetMaterials();
        tmpl.partFile = partIt->second;
        freeBuildInventory_.push_back(tmpl);
    }

    SPDLOG_INFO("BrickPlayer: FreeBuild inventory built with {} templates", freeBuildInventory_.size());
}

void BrickPlayerGameInstance::SpawnRandomBricks(int count)
{
    if (freeBuildInventory_.empty())
        return;

    auto& scene = GetEngine().GetScene();

    for (int i = 0; i < count; ++i)
    {
        // Pick random template
        int idx = std::rand() % static_cast<int>(freeBuildInventory_.size());
        const auto& tmpl = freeBuildInventory_[idx];

        uint32_t newId = scene.GenerateInstanceId();

        // Scatter outside the baseplate (-Z side), so bricks land on the table
        // Baseplate is ~320 LDU half-size, place bricks at Z = -400 to -600 LDU
        float lduScale = GetLduToWorldScale();
        float x = ((std::rand() % 600) - 300) * lduScale; // ±300 LDU in X
        float z = -(400.0f + (std::rand() % 200)) * lduScale; // -400 to -600 LDU in Z
        // Drop from ~1m above floor, staggered per brick
        float y = floorSurfaceY_ + 1.0f + static_cast<float>(i) * 0.12f
                  + (std::rand() % 50) * 0.005f;

        auto clone = Assets::SceneBuilder::CreateRenderNode(
            "freebuild_" + std::to_string(newId),
            glm::vec3(x, y, z),
            glm::vec3(1.0f),
            newId,
            tmpl.modelId,
            tmpl.materials);

        scene.AddNode(clone);

        auto bodyResult = CreateDynamicPhysicsBody(clone.get(), glm::vec3(1.0f), glm::quat(1, 0, 0, 0));
        if (bodyResult.created)
        {
            auto physComp = clone->GetComponent<Runtime::PhysicsComponent>();
            if (physComp)
                WakeLoosePartBody(NextEngine::GetInstance()->GetPhysicsEngine(), physComp->GetPhysicsBody(), newId);
            disassembledNodes_[newId] = {bodyResult.halfExtent};
        }

        // Register in tracking maps (step 1 = inventory, not baseplate)
        nodeStepMap_[newId] = 1;
        nodePartFileMap_[newId] = tmpl.partFile;

        // Capture assembly state for snap system
        OriginalAssemblyState state;
        state.parentInstanceId = UINT32_MAX;
        state.localTranslation = clone->Translation();
        state.localRotation = clone->Rotation();
        state.localScale = clone->Scale();
        state.worldTranslation = clone->WorldTranslation();
        state.worldRotation = clone->WorldRotation();
        state.worldScale = clone->WorldScale();
        const auto* model = scene.GetModel(tmpl.modelId);
        if (model)
        {
            state.worldAabbMin = model->GetLocalAABBMin();
            state.worldAabbMax = model->GetLocalAABBMax();
        }
        originalAssemblyStates_[newId] = state;
    }

    scene.MarkDirty();
    SPDLOG_INFO("BrickPlayer: spawned {} random bricks", count);
}

int BrickPlayerGameInstance::CountAvailableBricks()
{
    int count = 0;
    for (const auto& [id, info] : disassembledNodes_)
    {
        auto* node = GetEngine().GetScene().GetNodeByInstanceId(id);
        if (node)
            count++;
    }
    return count;
}

void BrickPlayerGameInstance::CreateFloorPhysicsBody()
{
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    hasFloorPlane_ = false;
    floorPlaneY_ = 0.0f;
    floorSurfaceY_ = 0.0f;
    if (!physics)
        return;

    float minY = FLT_MAX;
    auto& scene = GetEngine().GetScene();
    const auto& models = scene.Models();

    for (auto* render : scene.Components<Runtime::RenderComponent>())
    {
        Assets::Node* node = render->GetOwner();
        if (!node || !render->IsDrawable())
            continue;

        uint32_t modelIdx = render->GetModelId();
        if (modelIdx >= models.size())
            continue;

        const auto& model = models[modelIdx];
        const WorldBounds bounds =
            TransformLocalBounds(node->WorldTransform(), model.GetLocalAABBMin(), model.GetLocalAABBMax());
        minY = std::min(minY, bounds.min.y);
    }

    if (minY < FLT_MAX)
    {
        floorSurfaceY_ = minY;
        floorPlaneY_ = floorSurfaceY_;
        hasFloorPlane_ = true;
        physics->CreatePlaneBody(
            glm::vec3(0.0f, floorPlaneY_, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            NextMotionType::Static);
    }
}

// File dialog

void BrickPlayerGameInstance::OpenFileDialog()
{
    SDL_DialogFileFilter filters[] = {
        { "LDraw Files", "ldr;mpd" },
        { "All Files", "*" }
    };
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int filter)
        {
            auto* self = static_cast<BrickPlayerGameInstance*>(userdata);
            if (filelist && filelist[0])
            {
                self->currentScenePath_ = filelist[0];
                self->GetEngine().RequestLoadScene({.filename = self->currentScenePath_});
            }
        },
        this,
        GetEngine().GetWindow().Handle(),
        filters, 2, nullptr, false);
}
