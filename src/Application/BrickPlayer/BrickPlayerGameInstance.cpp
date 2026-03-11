#include "BrickPlayerGameInstance.hpp"
#include "BrickPlayerUserInterface.hpp"
#include "Assets/Loaders/FLDrawLoader.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Config/CVarSystem.hpp"

#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace
{
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
    cvars.SetDefaultFromString("r.dlss", "true", &error);
    cvars.SetDefaultFromString("r.dlssrr", "true", &error);
}

void BrickPlayerGameInstance::OnInit()
{
    if (!GOption->SceneName.empty())
    {
        currentScenePath_ = GOption->SceneName;
        GetEngine().RequestLoadScene(currentScenePath_);
    }
}

void BrickPlayerGameInstance::OnSceneLoaded()
{
    nodeStepMap_ = Assets::FLDrawLoader::GetLastLoadStepMap();
    totalSteps_ = Assets::FLDrawLoader::GetLastLoadTotalSteps();
    currentStep_ = 0;

    disassembledNodes_.clear();
    selectedInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().ClearSelection();

    CreateFloorPhysicsBody();

    // Auto-focus camera based on scene bounds
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
            continue;

        uint32_t modelIdx = render->GetModelId();
        if (modelIdx >= models.size())
            continue;

        glm::vec3 worldPos = node->WorldTranslation();
        center += worldPos;
        count++;

        const auto& model = models[modelIdx];
        glm::vec3 localMin = model.GetLocalAABBMin();
        glm::vec3 localMax = model.GetLocalAABBMax();
        glm::vec3 scale = node->WorldScale();
        float yLow = worldPos.y + localMin.y * scale.y;
        float yHigh = worldPos.y + localMax.y * scale.y;
        minY = std::min(minY, yLow);
        maxY = std::max(maxY, yHigh);
    }

    if (count > 0)
    {
        center /= static_cast<float>(count);
        cameraCenter_ = center;
        realCameraCenter_ = center;

        float sceneHeight = maxY - minY;
        cameraArm_ = std::max(1.0f, sceneHeight * 3.0f);
    }

    BuildPerPartOrder();

    // Auto-enable per-part mode if no build steps defined
    perPartMode_ = (totalSteps_ <= 1);

    UpdateVisibilityForStep(currentStep_);
    sceneLoaded_ = true;

    SPDLOG_INFO("BrickPlayer: loaded scene with {} steps, {} parts, per-part mode: {}",
                totalSteps_, totalParts_, perPartMode_ ? "on" : "off");
}

void BrickPlayerGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneLoaded_)
        return;

    // Auto-play
    if (autoPlay_)
    {
        float interval = playSpeedPresets[playSpeedIndex_].interval;
        autoPlayTimer_ += static_cast<float>(deltaSeconds);
        if (autoPlayTimer_ >= interval)
        {
            autoPlayTimer_ -= interval;
            int32_t maxStep = perPartMode_ ? totalParts_ : totalSteps_;
            if (currentStep_ < maxStep - 1)
            {
                StepForward();
            }
            else
            {
                autoPlay_ = false;
            }
        }
    }

    PerformRaycast();
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

    const_cast<BrickPlayerGameInstance*>(this)->panForward_ = glm::normalize(forward);
    const_cast<BrickPlayerGameInstance*>(this)->panLeft_ = glm::normalize(left);

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
            StepBackward();
            break;
        case SDLK_RIGHT:
            StepForward();
            break;
        case SDLK_SPACE:
        case SDLK_D:
            DisassembleSelected();
            break;
        case SDLK_R:
            ResetAll();
            break;
        case SDLK_F1:
            TogglePhysicsDebug();
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

    if (isOrbitDragging_ && mouseLeftDown_)
    {
        cameraRotX_ += static_cast<float>(delta.x) * cameraMultiplier_;
        cameraRotY_ += static_cast<float>(delta.y) * cameraMultiplier_;
        cameraRotY_ = std::clamp(cameraRotY_, -89.0f, 89.0f);
    }

    if (mouseRightDown_)
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
        isOrbitDragging_ = true;
        cameraMultiplier_ = 0.1f;
        return true;
    }
    else if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (!isOrbitDragging_ || cameraMultiplier_ < 0.001f)
        {
            // Was a click, not a drag - handled by raycast
        }
        mouseLeftDown_ = false;
        isOrbitDragging_ = false;
        return true;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
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
    const float scrollSpeed = 0.5f;
    cameraArm_ -= static_cast<float>(yoffset) * scrollSpeed;
    cameraArm_ = std::clamp(cameraArm_, 0.1f, 50.0f);
    return true;
}

void BrickPlayerGameInstance::OnRayHitResponse(Assets::RayCastResult& result)
{
    if (!result.Hitted)
        return;

    uint32_t instanceId = result.InstanceId;

    // Check if this node is in our step map (i.e., a LDraw part)
    if (nodeStepMap_.find(instanceId) == nodeStepMap_.end())
        return;

    // Check if already disassembled
    if (disassembledNodes_.count(instanceId))
        return;

    selectedInstanceId_ = instanceId;
    selectedHitNormal_ = glm::normalize(glm::vec3(result.Normal));
    GetEngine().GetScene().SetSelectedId(instanceId);
}

void BrickPlayerGameInstance::PerformRaycast()
{
    if (mouseCapturedByUI_)
        return;

    glm::vec3 dir = NextEngineHelper::ProjectScreenToWorld(mousePos_);
    GetEngine().RayCastGPU(cachedCameraPos_, dir, [this](Assets::RayCastResult result)
    {
        if (result.Hitted)
        {
            this->OnRayHitResponse(result);
        }
        return true;
    });
}

// Timeline

void BrickPlayerGameInstance::SetCurrentStep(int32_t step)
{
    int32_t maxStep = perPartMode_ ? totalParts_ : totalSteps_;
    if (maxStep <= 0)
        return;
    currentStep_ = std::clamp(step, 0, maxStep - 1);
    UpdateVisibilityForStep(currentStep_);
}

void BrickPlayerGameInstance::SetPerPartMode(bool enabled)
{
    if (perPartMode_ == enabled)
        return;
    perPartMode_ = enabled;
    autoPlay_ = false;

    // Reset to step 0 when switching modes
    currentStep_ = 0;
    UpdateVisibilityForStep(currentStep_);
}

void BrickPlayerGameInstance::ToggleAutoPlay()
{
    autoPlay_ = !autoPlay_;
    autoPlayTimer_ = 0.0f;

    // If starting auto-play and already at the end, restart from 0
    if (autoPlay_)
    {
        int32_t maxStep = perPartMode_ ? totalParts_ : totalSteps_;
        if (currentStep_ >= maxStep - 1)
        {
            currentStep_ = 0;
            UpdateVisibilityForStep(currentStep_);
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

void BrickPlayerGameInstance::StepForward()
{
    SetCurrentStep(currentStep_ + 1);
}

void BrickPlayerGameInstance::StepBackward()
{
    SetCurrentStep(currentStep_ - 1);
}

void BrickPlayerGameInstance::UpdateVisibilityForStep(int32_t step)
{
    auto& nodes = GetEngine().GetScene().Nodes();
    bool changed = false;

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
            if (render->GetVisible() != shouldBeVisible)
            {
                render->SetVisible(shouldBeVisible);
                render->SetRayCastVisible(shouldBeVisible);
                changed = true;
            }
        }
    }

    if (changed)
        GetEngine().GetScene().MarkDirty();
}

// Disassemble

void BrickPlayerGameInstance::DisassembleSelected()
{
    if (selectedInstanceId_ == UINT32_MAX)
        return;

    if (disassembledNodes_.count(selectedInstanceId_))
        return;

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
    selectedInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().ClearSelection();
    GetEngine().GetScene().MarkDirty();
}

void BrickPlayerGameInstance::ResetAll()
{
    if (!sceneLoaded_)
        return;

    // Reload the scene to restore all transforms and remove physics bodies
    sceneLoaded_ = false;
    disassembledNodes_.clear();
    selectedInstanceId_ = UINT32_MAX;
    GetEngine().GetScene().ClearSelection();

    // Re-request the same scene
    if (!currentScenePath_.empty())
        GetEngine().RequestLoadScene(currentScenePath_);
}

void BrickPlayerGameInstance::DrawPhysicsDebug()
{
#if WITH_PHYSIC
    if (!showPhysicsDebug_ || !sceneLoaded_)
        return;

    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!physics)
        return;

    // Build VP matrix from current camera
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    float aspect = vpSize.x / vpSize.y;
    glm::mat4 proj = glm::perspective(glm::radians(cameraFOV_), aspect, 0.2f, 2000.0f);
    glm::mat4 view = glm::lookAtRH(cachedCameraPos_, realCameraCenter_, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 vp = proj * view;

    auto* drawList = ImGui::GetForegroundDrawList();

    // Project world point to screen; returns false if behind camera
    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool
    {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f)
            return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        screenPos.x = vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x;
        screenPos.y = vpPos.y + (-ndc.y * 0.5f + 0.5f) * vpSize.y;
        return true;
    };

    // Draw a 3D wireframe edge
    auto drawEdge = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color)
    {
        ImVec2 sa, sb;
        if (projectToScreen(a, sa) && projectToScreen(b, sb))
        {
            drawList->AddLine(sa, sb, color, 1.5f);
        }
    };

    // Box corner indices for 12 edges
    static const int edges[12][2] = {
        {0,1},{1,3},{3,2},{2,0}, // bottom face
        {4,5},{5,7},{7,6},{6,4}, // top face
        {0,4},{1,5},{2,6},{3,7}  // vertical edges
    };

    for (auto& pair : disassembledNodes_)
    {
        uint32_t instanceId = pair.first;
        glm::vec3 he = pair.second.halfExtent;

        auto* node = GetEngine().GetScene().GetNodeByInstanceId(instanceId);
        if (!node)
            continue;

        auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
        if (!physComp)
            continue;

        auto* body = physics->GetBody(physComp->GetPhysicsBody());
        if (!body)
            continue;

        glm::vec3 pos = body->position;
        glm::quat rot = body->rotation;

        // 8 corners of the box in local space
        glm::vec3 localCorners[8] = {
            {-he.x, -he.y, -he.z},
            { he.x, -he.y, -he.z},
            {-he.x, -he.y,  he.z},
            { he.x, -he.y,  he.z},
            {-he.x,  he.y, -he.z},
            { he.x,  he.y, -he.z},
            {-he.x,  he.y,  he.z},
            { he.x,  he.y,  he.z},
        };

        // Transform corners to world space
        glm::vec3 worldCorners[8];
        for (int i = 0; i < 8; ++i)
        {
            worldCorners[i] = pos + rot * localCorners[i];
        }

        ImU32 color = IM_COL32(0, 255, 0, 200);
        for (const auto& edge : edges)
        {
            drawEdge(worldCorners[edge[0]], worldCorners[edge[1]], color);
        }

        // Draw center cross
        ImVec2 sc;
        if (projectToScreen(pos, sc))
        {
            drawList->AddCircle(sc, 4.0f, IM_COL32(255, 255, 0, 255), 8, 2.0f);
        }
    }
#endif
}

void BrickPlayerGameInstance::CreateFloorPhysicsBody()
{
#if WITH_PHYSIC
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!physics)
        return;

    // Find the floor y position from scene bounds
    float minY = FLT_MAX;
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

        glm::vec3 worldPos = node->WorldTranslation();
        const auto& model = models[modelIdx];
        float yLow = worldPos.y + model.GetLocalAABBMin().y * node->WorldScale().y;
        minY = std::min(minY, yLow);
    }

    if (minY < FLT_MAX)
    {
        physics->CreatePlaneBody(
            glm::vec3(0.0f, minY - 0.01f, 0.0f),
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
