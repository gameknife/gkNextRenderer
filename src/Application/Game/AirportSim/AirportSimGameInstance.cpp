#include "AirportSimGameInstance.hpp"

#include "AirportSimConfig.hpp"

#include <algorithm>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
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

    std::string initialScene = "assets/scad/airport.scad";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }
    SPDLOG_INFO("AirportSim: loading scene '{}'", initialScene);
    GetEngine().RequestLoadScene({.filename = initialScene});

    // 行为决策走本地 llama-server（§5.3）；不可用时全程 fallback。
    if (auto* ai = NextAI::GetAIService(GetEngine()))
    {
        const bool ok = ai->SwitchProvider(NextAI::EAIProviderType::LocalLlama);
        SPDLOG_INFO("AirportSim: SwitchProvider(LocalLlama) -> {} (provider='{}')", ok, ai->GetProviderName());
    }
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
        time_.Skip(4.5 * 60.0); // 05:00 -> 09:30
        time_.TimeScaleRef() = AirportSim::Config::kMaxTimeScale;
        ui_.State().llmEnabled = false;
        zoom_ = 0.6f; // 拉近一档，截图里能看清角色
        panOffset_ = glm::vec2(-4.0f, 4.0f); // 值机大厅~安检一带
    }

    sceneReady_ = true;
    SPDLOG_INFO("AirportSim: scene loaded ({} nodes, {} POIs)", scene.Nodes().size(), map_.Count());
}

void AirportSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
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
        queues_.Tick(gameMinutes);
        perception_.Tick(deltaSeconds, gameMinutes, agents_, queues_, flightBoard_);

        NextAI::FAIService* ai =
            (ui_.State().llmEnabled && !GOption->AgentValidation) ? NextAI::GetAIService(GetEngine()) : nullptr;
        scheduler_.Tick(gameMinutes, ai, agents_, map_, journey_, time_.IsNight());

        // 移动按真实时间积分，但随时间倍速缩放，保证调速时人流密度一致。
        const float moveDelta = static_cast<float>(deltaSeconds) *
                                (time_.TimeScaleRef() / AirportSim::Config::kDefaultTimeScale);
        agents_.Tick(moveDelta, scene);
    }
}

void AirportSimGameInstance::OnDestroy()
{
}

glm::vec3 AirportSimGameInstance::CameraTarget() const
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

glm::vec3 AirportSimGameInstance::CameraEye() const
{
    const bool following = ui_.State().followAgentId >= 0;
    const glm::vec3 offset = following ? kFollowOffset : kOverviewOffset * zoom_;
    return CameraTarget() + offset;
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

bool AirportSimGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    zoom_ = std::clamp(zoom_ - static_cast<float>(yoffset) * 0.08f, 0.45f, 1.6f);
    return true;
}

void AirportSimGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    //std::string error;
    //cvars.SetDefaultFromString("r.superResolution", "4", &error);
}
