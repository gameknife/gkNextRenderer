#include "BrickPlayerGameInstance.hpp"
#include "BrickPlayerSnapLogic.hpp"
#include "BrickPlayerUserInterface.hpp"
#include "Assets/Loaders/FLDrawLoader.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Config/CVarSystem.hpp"

#include <SDL3/SDL_dialog.h>
#include <spdlog/spdlog.h>
#include <glm/ext/scalar_constants.hpp>

namespace
{
    struct WorldBounds
    {
        glm::vec3 min{FLT_MAX};
        glm::vec3 max{-FLT_MAX};
    };

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
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<BrickPlayerGameInstance>(config, options, engine);
}

BrickPlayerGameInstance::BrickPlayerGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
    config.Title = "BrickPlayer";
    config.Width = 1920;
    config.Height = 960;
    config.HideTitleBar = true;

    options.ForceSDR = true;

    userInterface_ = std::make_unique<BrickPlayerUserInterface>(this);
}

void BrickPlayerGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "16", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    // cvars.SetDefaultFromString("r.dlss", "true", &error);
    // cvars.SetDefaultFromString("r.dlssrr", "true", &error);
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
    hoveredDisassembledInstanceId_ = UINT32_MAX;
    hoveredAssemblyInstanceId_ = UINT32_MAX;
    hoveredHitPoint_ = glm::vec3(0.0f);
    hoveredHitNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    hoveredAssemblyHitPoint_ = glm::vec3(0.0f);
    hoveredAssemblyHitNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
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
    return Assets::SanitizeLDrawLduToWorldScale(engine_->GetUserSettings().LDrawLduToWorldScale);
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

    auto& nodes = GetEngine().GetScene().Nodes();
    auto& models = GetEngine().GetScene().Models();
    for (auto& node : nodes)
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable())
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
        GetEngine().RequestLoadScene(currentScenePath_);
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
        for (auto& node : scene.Nodes())
        {
            uint32_t instanceId = node->GetInstanceId();
            if (node->GetName() == "ldraw_floor")
                continue;
            auto stepIt = nodeStepMap_.find(instanceId);
            if (stepIt == nodeStepMap_.end())
                continue;
            // Step 0 = baseplate, keep assembled and non-draggable
            if (stepIt->second <= 0)
                continue;

            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (!render || !render->IsDrawable())
                continue;

            uint32_t modelId = render->GetModelId();
            const auto* model = scene.GetModel(modelId);
            if (!model)
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

            // Create dynamic physics body
            glm::vec3 aabbMin = model->GetLocalAABBMin();
            glm::vec3 aabbMax = model->GetLocalAABBMax();
            glm::vec3 fullExtent = (aabbMax - aabbMin) * glm::abs(worldScale);
            fullExtent = glm::max(fullExtent, glm::vec3(0.002f));
            glm::vec3 halfExtent = fullExtent * 0.5f;
            glm::vec3 aabbCenter = (aabbMin + aabbMax) * 0.5f;
            glm::vec3 worldCenter = glm::vec3(node->WorldTransform() * glm::vec4(aabbCenter, 1.0f));
            glm::vec3 physicsOffset = aabbCenter;

            // Remove existing physics body if any
            auto existingPhys = node->GetComponent<Runtime::PhysicsComponent>();
#if WITH_PHYSIC
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
                phys->SetPhysicsOffset(physicsOffset);
                node->AddComponent(phys);
            }
#endif

            disassembledNodes_[instanceId] = {halfExtent};
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
        case SDLK_F1:
            TogglePhysicsDebug();
            break;
        case SDLK_F2:
            ToggleSnapDebug();
            break;
        default:
            break;
        }
    }
    return true;
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

    glm::vec3 dir = NextEngineHelper::ProjectScreenToWorld(mousePos_);
    bool handled = false;
    GetEngine().RayCastGPU(cachedCameraPos_, dir, [this, &handled](Assets::RayCastResult result)
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
    if (!result.Hitted)
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

        hoveredDisassembledInstanceId_ = UINT32_MAX;
        hoveredAssemblyInstanceId_ = instanceId;
        hoveredAssemblyHitPoint_ = glm::vec3(result.HitPoint);
        hoveredAssemblyHitNormal_ = hitNormal;
        GetEngine().GetScene().SetHoveredId(instanceId);
        return true;
    }

    if (disassembledNodes_.count(instanceId))
    {
        hoveredDisassembledInstanceId_ = instanceId;
        hoveredAssemblyInstanceId_ = UINT32_MAX;
        hoveredHitPoint_ = glm::vec3(result.HitPoint);
        hoveredHitNormal_ = hitNormal;
        GetEngine().GetScene().SetHoveredId(instanceId);
        return true;
    }

    hoveredDisassembledInstanceId_ = UINT32_MAX;
    hoveredAssemblyInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().ClearHoveredId();
    selectedInstanceId_ = instanceId;
    selectedHitNormal_ = hitNormal;
    GetEngine().GetScene().SetSelectedId(instanceId);
    return true;
}

bool BrickPlayerGameInstance::StartDraggingHoveredPart()
{
#if WITH_PHYSIC
    if (hoveredDisassembledInstanceId_ == UINT32_MAX)
    {
        return false;
    }

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!physics)
    {
        return false;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(hoveredDisassembledInstanceId_);
    if (!node)
    {
        return false;
    }

    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    if (!physComp)
    {
        return false;
    }

    auto* body = physics->GetBody(physComp->GetPhysicsBody());
    if (!body)
    {
        return false;
    }

    const glm::vec3 viewNormal =
        BrickPlayer::Snap::NormalizeOrDefault(realCameraCenter_ - cachedCameraPos_, glm::vec3(0.0f, 0.0f, 1.0f));

    draggedInstanceId_ = hoveredDisassembledInstanceId_;
    dragPlanePoint_ = hoveredHitPoint_;
    dragPlaneNormal_ = viewNormal;
    dragBodyOffset_ = body->position - hoveredHitPoint_;
    dragReleaseLinearVelocity_ = glm::vec3(0.0f);
    lastDraggedBodyPosition_ = body->position;
    hasDraggedBodyPositionSample_ = true;
    lockedDraggedConnectorIndex_ = FindDraggedConnectorLock(draggedInstanceId_, hoveredHitPoint_, hoveredHitNormal_);
    isDraggingPart_ = true;
    hoveredAssemblyInstanceId_ = UINT32_MAX;
    activeSnapCandidate_ = {};
    selectedInstanceId_ = draggedInstanceId_;
    selectedHitNormal_ = hoveredHitNormal_;
    SetDraggedPartRayCastVisible(false);
    GetEngine().GetScene().SetSelectedId(draggedInstanceId_);
    GetEngine().GetScene().SetHoveredId(draggedInstanceId_);
    return true;
#else
    return false;
#endif
}

void BrickPlayerGameInstance::StopDraggingPart()
{
#if WITH_PHYSIC
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

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    auto* node = GetEngine().GetScene().GetNodeByInstanceId(finishedInstanceId);
    if (!snappedBack && physics && node)
    {
        auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
        if (physComp)
        {
            auto* body = physics->GetBody(physComp->GetPhysicsBody());
            if (body)
            {
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
            }
        }

        SetDraggedPartRayCastVisible(true);
        hoveredDisassembledInstanceId_ = finishedInstanceId;
        GetEngine().GetScene().SetHoveredId(finishedInstanceId);
    }
    else if (snappedBack)
    {
        hoveredDisassembledInstanceId_ = UINT32_MAX;
        GetEngine().GetScene().ClearHoveredId();
    }
#endif

    hoveredAssemblyInstanceId_ = UINT32_MAX;
    ResetDragState();
}

void BrickPlayerGameInstance::UpdateDraggedPart()
{
#if WITH_PHYSIC
    if (!isDraggingPart_ || draggedInstanceId_ == UINT32_MAX)
    {
        return;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!node || !physics)
    {
        StopDraggingPart();
        return;
    }

    auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
    if (!physComp)
    {
        StopDraggingPart();
        return;
    }

    auto* body = physics->GetBody(physComp->GetPhysicsBody());
    if (!body)
    {
        StopDraggingPart();
        return;
    }

    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f};
    NextEngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);

    glm::vec3 planeHitPoint{0.0f};
    if (!IntersectDragPlane(rayOrigin, rayDir, planeHitPoint))
    {
        return;
    }

    const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * node->Scale();
    const BrickPlayer::Shadow::FSnapConnector* lockedDraggedConnector = GetLockedDraggedConnector(draggedInstanceId_);
    glm::vec3 dragAnchorToBodyOffset = dragBodyOffset_;
    if (lockedDraggedConnector)
    {
        dragAnchorToBodyOffset = -body->rotation * lockedDraggedConnector->localPosition + body->rotation * scaledOffset;
    }
    glm::vec3 freeBodyPosition = planeHitPoint + dragAnchorToBodyOffset;

    if (hoveredAssemblyInstanceId_ != UINT32_MAX)
    {
        const glm::vec3 hoverNormal =
            BrickPlayer::Snap::NormalizeOrDefault(hoveredAssemblyHitNormal_, glm::vec3(0.0f, 1.0f, 0.0f));
        const float lduToWorldScale = GetLduToWorldScale();
        const float surfaceGap = std::max(8.0f * lduToWorldScale * 0.15f, 0.002f);
        if (lockedDraggedConnector)
        {
            const glm::vec3 connectorAnchor = hoveredAssemblyHitPoint_ + hoverNormal * surfaceGap;
            freeBodyPosition = connectorAnchor - body->rotation * lockedDraggedConnector->localPosition + body->rotation * scaledOffset;
        }
        else
        {
            const auto disassembledIt = disassembledNodes_.find(draggedInstanceId_);
            const glm::vec3 halfExtent = disassembledIt != disassembledNodes_.end()
                ? disassembledIt->second.halfExtent
                : glm::vec3(0.01f);
            const float supportDistance = ProjectHalfExtentOnAxis(halfExtent, body->rotation, hoverNormal);
            freeBodyPosition = hoveredAssemblyHitPoint_ + hoverNormal * (supportDistance + surfaceGap);
        }
    }
    else
    {
        float minBodyY = 0.0f;
        if (GetDraggedBodyMinimumY(node, body->rotation, minBodyY) && freeBodyPosition.y < minBodyY)
        {
            glm::vec3 floorAnchorPoint{0.0f};
            const float anchorPlaneY = minBodyY - dragAnchorToBodyOffset.y;
            if (IntersectHorizontalPlane(rayOrigin, rayDir, anchorPlaneY, floorAnchorPoint))
            {
                freeBodyPosition = floorAnchorPoint + dragAnchorToBodyOffset;
            }
            freeBodyPosition.y = minBodyY;
        }
    }
    ClampDraggedBodyPositionAboveFloor(node, body->rotation, freeBodyPosition);

    glm::vec3 desiredBodyPosition = freeBodyPosition;
    glm::quat desiredBodyRotation = body->rotation;
    const DragSnapCandidate previousSnapCandidate = activeSnapCandidate_;

    DragSnapCandidate snapCandidate;
    if (TryBuildSnapCandidate(node, physComp, freeBodyPosition, snapCandidate)
        && !ClampDraggedBodyPositionAboveFloor(node, snapCandidate.desiredRotation, snapCandidate.desiredBodyPosition))
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
            snapFeedbackPulseUntil_ = engine_->GetTime() + 0.12f;
        }
        if (snapAudioChanged)
        {
            PlayRandomPutSound();
        }
        desiredBodyPosition = snapCandidate.desiredBodyPosition;
        desiredBodyRotation = snapCandidate.desiredRotation;
        hoveredHitPoint_ = snapCandidate.desiredTranslation;
    }
    else
    {
        activeSnapCandidate_ = {};
        hoveredHitPoint_ = hoveredAssemblyInstanceId_ != UINT32_MAX ? hoveredAssemblyHitPoint_ : planeHitPoint;
    }

    const float deltaSeconds = std::max(engine_->GetDeltaSeconds(), 1.0f / 240.0f);
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

    hoveredDisassembledInstanceId_ = draggedInstanceId_;
    if (activeSnapCandidate_.valid)
    {
        GetEngine().GetScene().SetHoveredId(activeSnapCandidate_.targetInstanceId);
    }
    else if (hoveredAssemblyInstanceId_ != UINT32_MAX)
    {
        GetEngine().GetScene().SetHoveredId(hoveredAssemblyInstanceId_);
    }
    else
    {
        GetEngine().GetScene().ClearHoveredId();
    }
    GetEngine().GetScene().SetSelectedId(draggedInstanceId_);
    GetEngine().GetScene().MarkDirty();
#endif
}

bool BrickPlayerGameInstance::TryBuildSnapCandidate(Assets::Node* node,
                                                    const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
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
                                                          const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
                                                          const glm::vec3& freeBodyPosition,
                                                          DragSnapCandidate& outCandidate)
{
    outCandidate = {};

    if (!node || !physComp || hoveredAssemblyInstanceId_ == UINT32_MAX)
    {
        return false;
    }

    auto partIt = nodePartFileMap_.find(draggedInstanceId_);
    if (partIt == nodePartFileMap_.end())
    {
        return false;
    }

    const auto& draggedConnectors = shadowLibrary_.GetConnectorsForPart(partIt->second);
    const std::vector<WorldSnapConnector> targetConnectors = BuildWorldConnectors(hoveredAssemblyInstanceId_);
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
                                                           hoveredAssemblyHitPoint_,
                                                           hoveredAssemblyHitNormal_,
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
                    const float distanceToSnap = glm::distance(freeBodyPosition, desiredBodyPosition);
                    if (distanceToSnap > snapThreshold)
                    {
                        continue;
                    }

                    const int connectivityScore = ScoreShadowCandidate(
                        draggedInstanceId_,
                        desiredRotation,
                        desiredTranslation,
                        targetConnector);
                    const int hoverScore = static_cast<int>(std::round(
                        (hoverFilter.hoverDistanceLimit - hoverFilter.hoverDistance) * 100.0f));
                    const int depthScore = static_cast<int>(std::round(
                        (hoverFilter.hoverDepthLimit - glm::abs(hoverFilter.hoverDepth)) * 100.0f));
                    const int normalScore = static_cast<int>(std::round(
                        glm::dot(targetConnector.worldAxis, hoverFilter.hoverNormal) * 1000.0f));
                    const int lockScore = isLockedConnector ? 5000 : 0;
                    const int rotationScore =
                        static_cast<int>(std::round(glm::abs(glm::dot(desiredRotation, currentDragRotation)) * 1200.0f));
                    const bool matchesActiveCandidate = activeSnapCandidate_.valid
                        && !activeSnapCandidate_.restoreOriginalHierarchy
                        && activeSnapCandidate_.targetInstanceId == targetConnector.ownerInstanceId
                        && activeSnapCandidate_.draggedConnectorIndex == static_cast<int32_t>(draggedConnectorIndex)
                        && activeSnapCandidate_.targetConnectorIndex == static_cast<int32_t>(targetConnectorIndex);
                    const int stickyScore = matchesActiveCandidate ? 6000 : 0;
                    const int score =
                        connectivityScore * 10000 + normalScore + depthScore + hoverScore + lockScore + rotationScore + stickyScore;

                    const int replaceMargin = bestMatchesActiveCandidate && !matchesActiveCandidate ? 1500 : 0;
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

bool BrickPlayerGameInstance::TryBuildOriginalSnapCandidate(const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
                                                            const glm::vec3& freeBodyPosition,
                                                            DragSnapCandidate& outCandidate)
{
    outCandidate = {};

    if (!physComp || hoveredAssemblyInstanceId_ == UINT32_MAX)
    {
        return false;
    }

    if (!AreNodesOriginallyConnectable(draggedInstanceId_, hoveredAssemblyInstanceId_))
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
    outCandidate.targetInstanceId = hoveredAssemblyInstanceId_;
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

    auto* node = engine_->GetScene().GetNodeByInstanceId(instanceId);
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

int BrickPlayerGameInstance::ScoreShadowCandidate(uint32_t draggedId,
                                                  const glm::quat& desiredRotation,
                                                  const glm::vec3& desiredTranslation,
                                                  const WorldSnapConnector& anchorTarget) const
{
    auto partIt = nodePartFileMap_.find(draggedId);
    if (partIt == nodePartFileMap_.end())
    {
        return 0;
    }

    const auto& draggedConnectors = shadowLibrary_.GetConnectorsForPart(partIt->second);
    if (draggedConnectors.empty())
    {
        return 0;
    }

    const BrickPlayer::Snap::FScaleMetrics scaleMetrics =
        BrickPlayer::Snap::BuildScaleMetrics(GetLduToWorldScale());
    const float matchDistance = std::max(scaleMetrics.studPitch * 0.3f, 0.004f);
    const float searchRadius = std::max(scaleMetrics.studPitch * 6.0f, GetSnapDistanceThreshold(draggedId) * 6.0f);
    const float axisDotThreshold = 0.96f;

    std::vector<WorldSnapConnector> nearbyTargets;
    for (const auto& node : engine_->GetScene().Nodes())
    {
        const uint32_t nodeId = node->GetInstanceId();
        if (nodeId == draggedId || disassembledNodes_.count(nodeId) || nodeStepMap_.find(nodeId) == nodeStepMap_.end())
        {
            continue;
        }

        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable() || !render->GetVisible())
        {
            continue;
        }

        if (glm::distance(node->WorldTranslation(), anchorTarget.worldPosition) > searchRadius)
        {
            continue;
        }

        std::vector<WorldSnapConnector> nodeConnectors = BuildWorldConnectors(nodeId);
        nearbyTargets.insert(nearbyTargets.end(), nodeConnectors.begin(), nodeConnectors.end());
    }

    int matchedConnectorCount = 0;
    for (const BrickPlayer::Shadow::FSnapConnector& draggedConnector : draggedConnectors)
    {
        const glm::quat worldRotation = glm::normalize(desiredRotation * draggedConnector.localRotation);
        const glm::vec3 worldAxis = glm::normalize(worldRotation * glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 worldPosition = desiredTranslation + desiredRotation * draggedConnector.localPosition;

        float bestMatchDistance = FLT_MAX;
        bool matched = false;
        for (const WorldSnapConnector& targetConnector : nearbyTargets)
        {
            if (!targetConnector.connector
                || !BrickPlayer::Snap::AreConnectorsCompatible(draggedConnector,
                                                               *targetConnector.connector,
                                                               scaleMetrics.lduToWorldScale))
            {
                continue;
            }

            if (glm::dot(worldAxis, targetConnector.worldAxis) < axisDotThreshold)
            {
                continue;
            }

            const float distance = glm::distance(worldPosition, targetConnector.worldPosition);
            if (distance > matchDistance || distance >= bestMatchDistance)
            {
                continue;
            }

            bestMatchDistance = distance;
            matched = true;
        }

        if (matched)
        {
            matchedConnectorCount++;
        }
    }

    return matchedConnectorCount;
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

    return std::max(std::max(scaleMetrics.studPitch * 1.25f, scaleMetrics.plateHeight * 2.0f), sizeBasedThreshold);
}

void BrickPlayerGameInstance::SetDraggedPartRayCastVisible(bool visible)
{
    if (draggedInstanceId_ == UINT32_MAX)
    {
        return;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
    if (!node)
    {
        return;
    }

    auto render = node->GetComponent<Runtime::RenderComponent>();
    if (!render || render->GetRayCastVisible() == visible)
    {
        return;
    }

    render->SetRayCastVisible(visible);
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
    SetDraggedPartRayCastVisible(true);

    disassembledNodes_.erase(draggedInstanceId_);
    selectedInstanceId_ = draggedInstanceId_;
    GetEngine().GetScene().SetSelectedId(draggedInstanceId_);
    GetEngine().GetScene().MarkDirty();
    return true;
}

void BrickPlayerGameInstance::PlayRandomPutSound()
{
    engine_->PlaySound(GetRandomPutSoundPath(), false, 0.55f);
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

    const auto* model = engine_->GetScene().GetModel(render->GetModelId());
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
        GetEngine().PauseSound(currentTrack->path, true);
    }

    currentBGM_ = (currentBGM_ + 1) % static_cast<uint32_t>(bgmTracks_.size());
    currentTrack = GetCurrentBGMTrack();
    if (currentTrack && !currentTrack->path.empty())
    {
        GetEngine().PlaySound(currentTrack->path, true, 0.45f);
    }
}

bool BrickPlayerGameInstance::IsBGMPaused() const
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return true;
    }

    return !engine_->IsSoundPlaying(currentTrack->path);
}

void BrickPlayerGameInstance::PauseBGM(bool pause)
{
    const BGMTrack* currentTrack = GetCurrentBGMTrack();
    if (!currentTrack)
    {
        return;
    }

    GetEngine().PauseSound(currentTrack->path, pause);
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

    auto& nodes = GetEngine().GetScene().Nodes();
    for (auto& node : nodes)
    {
        uint32_t instanceId = node->GetInstanceId();
        auto it = nodeStepMap_.find(instanceId);
        if (it == nodeStepMap_.end())
            continue;

        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable())
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

    for (const auto& node : scene.Nodes())
    {
        const uint32_t instanceId = node->GetInstanceId();
        if (nodeStepMap_.find(instanceId) == nodeStepMap_.end())
        {
            continue;
        }

        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable())
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
    auto& nodes = GetEngine().GetScene().Nodes();
    bool changed = false;
    int32_t revealedCount = 0;

    const auto& lookupMap = perPartMode_ ? nodePartOrder_ : nodeStepMap_;

    for (auto& node : nodes)
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render)
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
            if (wasVisible != shouldBeVisible)
            {
                render->SetVisible(shouldBeVisible);
                render->SetRayCastVisible(shouldBeVisible);
                changed = true;
                if (playPlacementSound && shouldBeVisible)
                {
                    ++revealedCount;
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

    auto render = node->GetComponent<Runtime::RenderComponent>();
    if (!render || !render->IsDrawable())
        return;

    uint32_t modelId = render->GetModelId();
    const auto* model = GetEngine().GetScene().GetModel(modelId);
    if (!model)
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

    // Remove any existing physics body before creating a new one
    auto existingPhys = node->GetComponent<Runtime::PhysicsComponent>();
#if WITH_PHYSIC
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (existingPhys && physics)
    {
        NextBodyID oldBody = existingPhys->GetPhysicsBody();
        if (!oldBody.IsInvalid())
        {
            physics->RemoveBody(oldBody);
        }
    }
#endif

    glm::vec3 aabbMin = model->GetLocalAABBMin();
    glm::vec3 aabbMax = model->GetLocalAABBMax();
    // CreateBoxBody expects FULL extent (it halves internally for Jolt's BoxShape)
    glm::vec3 fullExtent = (aabbMax - aabbMin) * glm::abs(worldScale);
    fullExtent = glm::max(fullExtent, glm::vec3(0.002f));
    glm::vec3 halfExtent = fullExtent * 0.5f;
    glm::vec3 aabbCenter = (aabbMin + aabbMax) * 0.5f;
    glm::vec3 worldCenter = glm::vec3(node->WorldTransform() * glm::vec4(aabbCenter, 1.0f));
    // Physics offset is stored in node local space. Node::TickVelocity applies scale and rotation.
    glm::vec3 physicsOffset = aabbCenter;

#if WITH_PHYSIC
    if (physics)
    {
        auto phys = std::make_shared<Runtime::PhysicsComponent>();
        phys->SetMobility(Runtime::ENodeMobility::Dynamic);
        auto bodyId = physics->CreateBoxBody(worldCenter, worldRotation, fullExtent, NextMotionType::Dynamic);
        phys->BindPhysicsBody(bodyId);
        phys->SetPhysicsOffset(physicsOffset);
        node->AddComponent(phys);

        // Apply force along the hit surface normal so the part pops outward
        float volume = fullExtent.x * fullExtent.y * fullExtent.z;
        float impulseMagnitude = std::max(volume * 80000.0f, 800.0f);
        glm::vec3 forceDir = glm::length(selectedHitNormal_) > 0.001f
            ? glm::normalize(selectedHitNormal_)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        physics->AddForceToBody(bodyId, forceDir * impulseMagnitude);
    }
#endif

    disassembledNodes_[selectedInstanceId_] = {halfExtent};
    hoveredDisassembledInstanceId_ = UINT32_MAX;
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
        GetEngine().RequestLoadScene(currentScenePath_);
}

void BrickPlayerGameInstance::StartFreeBuild()
{
    currentScenePath_ = "assets/omr/freebuild.ldr";
    GetEngine().RequestLoadScene(currentScenePath_);
}

void BrickPlayerGameInstance::BuildFreeBuildInventory()
{
    freeBuildInventory_.clear();
    auto& scene = GetEngine().GetScene();

    for (auto& node : scene.Nodes())
    {
        if (node->GetName() == "ldraw_floor")
            continue;

        uint32_t instanceId = node->GetInstanceId();

        // Only include inventory bricks (step > 0), skip baseplates (step 0)
        auto stepIt = nodeStepMap_.find(instanceId);
        if (stepIt == nodeStepMap_.end() || stepIt->second <= 0)
            continue;

        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable())
            continue;

        auto partIt = nodePartFileMap_.find(instanceId);
        if (partIt == nodePartFileMap_.end())
            continue;

        InventoryTemplate tmpl;
        tmpl.sourceInstanceId = instanceId;
        tmpl.modelId = render->GetModelId();
        tmpl.materials = render->Materials();
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

        auto clone = Assets::Node::CreateNode(
            "freebuild_" + std::to_string(newId),
            glm::vec3(x, y, z),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f),
            newId);

        auto newRender = std::make_shared<Runtime::RenderComponent>();
        newRender->SetModelId(tmpl.modelId);
        newRender->SetMaterial(tmpl.materials);
        newRender->SetVisible(true);
        newRender->SetRayCastVisible(true);
        clone->AddComponent(newRender);

        scene.AddNode(clone);

        // Register in tracking maps (step 1 = inventory, not baseplate)
        nodeStepMap_[newId] = 1;
        nodePartFileMap_[newId] = tmpl.partFile;

        // Create dynamic physics body
        const auto* model = scene.GetModel(tmpl.modelId);
        if (model)
        {
            glm::vec3 aabbMin = model->GetLocalAABBMin();
            glm::vec3 aabbMax = model->GetLocalAABBMax();
            glm::vec3 fullExtent = aabbMax - aabbMin;
            fullExtent = glm::max(fullExtent, glm::vec3(0.002f));
            glm::vec3 halfExtent = fullExtent * 0.5f;
            glm::vec3 aabbCenter = (aabbMin + aabbMax) * 0.5f;
            glm::vec3 worldCenter = glm::vec3(clone->WorldTransform() * glm::vec4(aabbCenter, 1.0f));

#if WITH_PHYSIC
            auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
            if (physics)
            {
                auto phys = std::make_shared<Runtime::PhysicsComponent>();
                phys->SetMobility(Runtime::ENodeMobility::Dynamic);
                auto bodyId = physics->CreateBoxBody(worldCenter, glm::quat(1, 0, 0, 0), fullExtent, NextMotionType::Dynamic);
                phys->BindPhysicsBody(bodyId);
                phys->SetPhysicsOffset(aabbCenter);
                clone->AddComponent(phys);
            }
#endif

            disassembledNodes_[newId] = {halfExtent};
        }

        // Capture assembly state for snap system
        OriginalAssemblyState state;
        state.parentInstanceId = UINT32_MAX;
        state.localTranslation = clone->Translation();
        state.localRotation = clone->Rotation();
        state.localScale = clone->Scale();
        state.worldTranslation = clone->WorldTranslation();
        state.worldRotation = clone->WorldRotation();
        state.worldScale = clone->WorldScale();
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
        if (node && node->GetName() != "ldraw_floor")
            count++;
    }
    return count;
}

void BrickPlayerGameInstance::CreateFloorPhysicsBody()
{
#if WITH_PHYSIC
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    hasFloorPlane_ = false;
    floorPlaneY_ = 0.0f;
    floorSurfaceY_ = 0.0f;
    if (!physics)
        return;

    // Prefer the auto-generated LDraw floor top surface; otherwise fall back to scene bounds.
    float minY = FLT_MAX;
    float floorTopY = FLT_MAX;
    auto& nodes = GetEngine().GetScene().Nodes();
    auto& models = GetEngine().GetScene().Models();

    for (auto& node : nodes)
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render || !render->IsDrawable())
            continue;

        uint32_t modelIdx = render->GetModelId();
        if (modelIdx >= models.size())
            continue;

        const auto& model = models[modelIdx];
        const WorldBounds bounds =
            TransformLocalBounds(node->WorldTransform(), model.GetLocalAABBMin(), model.GetLocalAABBMax());
        if (node->GetName() == "ldraw_floor")
        {
            floorTopY = bounds.max.y;
            continue;
        }

        minY = std::min(minY, bounds.min.y);
    }

    if (floorTopY < FLT_MAX || minY < FLT_MAX)
    {
        floorSurfaceY_ = floorTopY < FLT_MAX ? floorTopY : minY;
        floorPlaneY_ = floorSurfaceY_;
        hasFloorPlane_ = true;
        physics->CreatePlaneBody(
            glm::vec3(0.0f, floorPlaneY_, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            NextMotionType::Static);
    }
#endif
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
                self->GetEngine().RequestLoadScene(self->currentScenePath_);
            }
        },
        this,
        GetEngine().GetWindow().Handle(),
        filters, 2, nullptr, false);
}
