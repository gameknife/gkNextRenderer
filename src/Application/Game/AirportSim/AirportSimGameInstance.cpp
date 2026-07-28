#include "AirportSimGameInstance.hpp"

#include "AirportSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "Modules/NextAI/NextAIModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

namespace
{
    constexpr float kFov = 50.0f;
    // 俯视总览：覆盖航站楼 + 陆侧（引擎系：x∈[-42,42]，z = −scad_y ∈ [-47,33]）。
    constexpr glm::vec3 kOverviewTarget{0.0f, 0.0f, -2.0f};
    constexpr glm::vec3 kOverviewOffset{0.0f, 58.0f, 44.0f};
    constexpr glm::vec3 kFollowOffset{0.0f, 9.0f, 9.0f};
    constexpr float kAgentPickRadiusPixels = 18.0f;
    constexpr float kCameraTransitionSharpness = 9.0f;

    bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& viewportPos, const ImVec2& viewportSize,
                      const glm::vec3& world, glm::vec2& outScreen)
    {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
        {
            return false;
        }
        outScreen = {
            viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
            viewportPos.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y,
        };
        return true;
    }

    float DistanceToSegment(const glm::vec2& point, const glm::vec2& start, const glm::vec2& end)
    {
        const glm::vec2 segment = end - start;
        const float lengthSquared = glm::dot(segment, segment);
        if (lengthSquared <= 0.001f)
        {
            return glm::distance(point, start);
        }
        const float t = std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
        return glm::distance(point, start + segment * t);
    }
}

// Each game executable provides this factory; DesktopMain.cpp binds it at link time.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register(); // .scad 场景加载器
    return std::make_unique<AirportSimGameInstance>(config, options, engine);
}

AirportSimGameInstance::AirportSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                               NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "AirportSim", 1920, 1080, true);
}

void AirportSimGameInstance::OnInit()
{
    // NavGrid 从场景 CPU BVH 构建，需保留 CPU 网格数据。
    GOption->KeepCPUMeshData = true;

    std::string initialScene = "assets/scad/source/airport.scad";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }
    SPDLOG_INFO("AirportSim: loading scene '{}'", initialScene);
    GetEngine().RequestLoadScene({.filename = initialScene});

    // 行为决策走本地 llama-server；不可用时全程 fallback。
    if (auto* ai = NextAI::GetAIService(GetEngine())) ai->SetProfile("simulation");
}

void AirportSimGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                                std::vector<Assets::Model>& models,
                                                std::vector<Assets::FMaterial>& materials,
                                                std::vector<Assets::LightObject>& /*lights*/,
                                                std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    agents_.InjectAssets(models, materials);
}

void AirportSimGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    ui_.State().followAgentId = -1;
    map_.BuildFromScene(scene);
    agents_.OnSceneLoaded(scene);

    time_.Reset();
    flightBoard_.Reset(20260611u);
    journey_.Reset(42u);
    queues_.Reset(7u);
    perception_.Reset();
    scheduler_.Reset();

    if (GOption->AgentValidation)
    {
        // 截图验证：直接跳到客流高峰并提速，让画面里有人。
        time_.Skip(1.5 * 60.0); // 05:00 -> 06:30，保留旅客分批到达过程
        time_.TimeScaleRef() = AirportSim::Config::kMaxTimeScale;
        ui_.State().llmEnabled = false;
        zoom_ = 0.6f; // 拉近一档，截图里能看清角色
        panOffset_ = glm::vec2(-4.0f, 4.0f); // 值机大厅~安检一带
    }

    cameraTarget_ = DesiredCameraTarget();
    cameraEye_ = DesiredCameraEye();
    cameraInitialized_ = true;
    sceneReady_ = true;
    SPDLOG_INFO("AirportSim: scene loaded ({} nodes, {} POIs)", scene.Nodes().size(), map_.Count());
}

void AirportSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    cameraInitialized_ = false;
    ui_.State().followAgentId = -1;
    map_.Clear();
    agents_.Clear();
    scheduler_.Reset();
}

void AirportSimGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        return;
    }
    Assets::Scene& scene = GetEngine().GetScene();
    time_.Tick(deltaSeconds, scene);

    const double gameMinutes = time_.GameMinutes();
    const double dayMinutes = time_.DayMinutes();
    const bool paused = time_.PausedRef();

    if (!paused)
    {
        flightBoard_.Tick(time_.DayIndex(), dayMinutes);
        journey_.Tick(gameMinutes, dayMinutes, time_.DayIndex(), agents_, map_, queues_, flightBoard_, time_);
        queues_.Tick(gameMinutes, agents_);
        perception_.Tick(deltaSeconds, gameMinutes, agents_, queues_, flightBoard_);

        NextAI::FAIService* ai =
            (ui_.State().llmEnabled && !GOption->AgentValidation) ? NextAI::GetAIService(GetEngine()) : nullptr;
        scheduler_.Tick(gameMinutes, ai, agents_, map_, journey_, time_.IsNight());

        // 移动按真实时间积分，但随时间倍速缩放，保证调速时人流密度一致。
        const float moveDelta = static_cast<float>(deltaSeconds) *
                                (time_.TimeScaleRef() / AirportSim::Config::kDefaultTimeScale);
        agents_.Tick(moveDelta, scene);
    }

    if (ui_.State().followAgentId >= 0 && agents_.FindById(ui_.State().followAgentId) == nullptr)
    {
        ui_.State().followAgentId = -1;
    }

    UpdateCamera(deltaSeconds);
}

void AirportSimGameInstance::OnDestroy()
{
}

glm::vec3 AirportSimGameInstance::CameraTarget() const
{
    return cameraInitialized_ ? cameraTarget_ : DesiredCameraTarget();
}

glm::vec3 AirportSimGameInstance::CameraEye() const
{
    return cameraInitialized_ ? cameraEye_ : DesiredCameraEye();
}

glm::vec3 AirportSimGameInstance::DesiredCameraTarget() const
{
    if (ui_.State().followAgentId >= 0)
    {
        for (const auto& agent : agents_.Agents())
        {
            if (agent.active && agent.id == ui_.State().followAgentId)
            {
                return agent.position;
            }
        }
    }
    return kOverviewTarget + glm::vec3(panOffset_.x, 0.0f, panOffset_.y);
}

glm::vec3 AirportSimGameInstance::DesiredCameraEye() const
{
    const bool following = ui_.State().followAgentId >= 0;
    const glm::vec3 offset = following ? kFollowOffset : kOverviewOffset * zoom_;
    return DesiredCameraTarget() + offset;
}

void AirportSimGameInstance::UpdateCamera(double deltaSeconds)
{
    const glm::vec3 desiredTarget = DesiredCameraTarget();
    const glm::vec3 desiredEye = DesiredCameraEye();
    if (!cameraInitialized_)
    {
        cameraTarget_ = desiredTarget;
        cameraEye_ = desiredEye;
        cameraInitialized_ = true;
        return;
    }

    const float lerpFactor =
        1.0f - std::exp(-kCameraTransitionSharpness * std::max(0.0f, static_cast<float>(deltaSeconds)));
    cameraTarget_ = glm::mix(cameraTarget_, desiredTarget, lerpFactor);
    cameraEye_ = glm::mix(cameraEye_, desiredEye, lerpFactor);
}

glm::mat4 AirportSimGameInstance::ViewMatrix() const
{
    return glm::lookAt(CameraEye(), CameraTarget(), glm::vec3(0.0f, 1.0f, 0.0f));
}

bool AirportSimGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = ViewMatrix();
    outRenderCamera.FieldOfView = kFov;
    return true;
}

bool AirportSimGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return true;
    }

    const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    const float aspect = vpSize.y > 1.0f ? vpSize.x / vpSize.y : 16.0f / 9.0f;
    const glm::mat4 viewProjection = glm::perspective(glm::radians(kFov), aspect, 0.05f, 2000.0f) * ViewMatrix();

    ui_.SetCameraEye(CameraEye());
    const bool llmConnected = ui_.State().llmEnabled && NextAI::GetAIService(GetEngine()) != nullptr;
    ui_.Draw(viewProjection, time_.GameMinutes(), time_, flightBoard_, agents_, map_, queues_, scheduler_,
             llmConnected);
    return true;
}

bool AirportSimGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return false;
    }
    switch (event.key.key)
    {
    case SDLK_F8:
        ui_.State().showDebugPanel = !ui_.State().showDebugPanel;
        return true;
    case SDLK_ESCAPE:
        if (ui_.State().followAgentId >= 0)
        {
            ui_.State().followAgentId = -1;
            return true;
        }
        return false;
    case SDLK_SPACE:
        time_.PausedRef() = !time_.PausedRef();
        return true;
    case SDLK_LEFT:
        panOffset_.x = std::max(panOffset_.x - 4.0f, -40.0f);
        return true;
    case SDLK_RIGHT:
        panOffset_.x = std::min(panOffset_.x + 4.0f, 40.0f);
        return true;
    case SDLK_UP:
        panOffset_.y = std::max(panOffset_.y - 4.0f, -45.0f);
        return true;
    case SDLK_DOWN:
        panOffset_.y = std::min(panOffset_.y + 4.0f, 35.0f);
        return true;
    default:
        return false;
    }
}

bool AirportSimGameInstance::SupportsAppDebugShortcut(SDL_Keycode key) const
{
    return key == SDLK_F5;
}

bool AirportSimGameInstance::IsAppDebugShortcutActive(SDL_Keycode key) const
{
    return key == SDLK_F5 && ui_.State().showPoiMarkers;
}

bool AirportSimGameInstance::SetAppDebugShortcutActive(SDL_Keycode key, bool active)
{
    if (key != SDLK_F5)
    {
        return false;
    }
    ui_.State().showPoiMarkers = active;
    return true;
}

int AirportSimGameInstance::PickAgentAtScreen(const glm::vec2& screenPos) const
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
    {
        return -1;
    }

    const float aspect = viewport->Size.x / viewport->Size.y;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kFov), aspect, 0.05f, 2000.0f) * ViewMatrix();

    int pickedId = -1;
    float nearestDistance = kAgentPickRadiusPixels;
    for (const auto& agent : agents_.Agents())
    {
        if (!agent.active)
        {
            continue;
        }

        glm::vec2 feetScreen;
        glm::vec2 headScreen;
        if (!ProjectWorld(viewProjection, viewport->Pos, viewport->Size,
                          agent.position + glm::vec3(0.0f, 0.1f, 0.0f), feetScreen) ||
            !ProjectWorld(viewProjection, viewport->Pos, viewport->Size,
                          agent.position + glm::vec3(0.0f, 2.1f, 0.0f), headScreen))
        {
            continue;
        }

        const float distance = DistanceToSegment(screenPos, feetScreen, headScreen);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            pickedId = agent.id;
        }
    }
    return pickedId;
}

bool AirportSimGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!sceneReady_ || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    const ImVec2 mousePos = ImGui::GetMousePos();
    ui_.State().followAgentId = PickAgentAtScreen(glm::vec2(mousePos.x, mousePos.y));
    return true;
}

bool AirportSimGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    zoom_ = std::clamp(zoom_ - static_cast<float>(yoffset) * 0.08f, 0.45f, 1.6f);
    return true;
}

void AirportSimGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.samples", "4", &error);
    cvars.SetDefaultFromString("r.sharc.enable", "true", &error);
    //cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "1", &error);
}
