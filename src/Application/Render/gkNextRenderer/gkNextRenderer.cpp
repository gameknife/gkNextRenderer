#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"

#include <SDL3/SDL_misc.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#if GK_WITH_VITURE
#include "Modules/DevTools/VitureDebugPanel.hpp"
#include "Modules/NextViture/VitureModule.hpp"
#endif
#include "Application/Common/DemoScenes.hpp"


namespace
{
using Utilities::FormatBytes;
using NextUI::Theme::DrawFlatViewportButton;
using NextUI::Theme::DrawViewportComboOption;
using NextUI::Theme::PopViewportPopupStyle;
using NextUI::Theme::PopViewportToolbarStyle;
using NextUI::Theme::PushViewportPopupStyle;
using NextUI::Theme::PushViewportToolbarStyle;

constexpr uint32_t dropSphereGridSize = 20;
constexpr uint32_t dropSphereCount = dropSphereGridSize * dropSphereGridSize;

uint32_t HashUint(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}
float HashToUnitFloat(uint32_t value)
{
    return static_cast<float>(HashUint(value) >> 8) * (1.0f / 16777216.0f);
}

glm::vec3 HsvToRgb(float hue, float saturation, float value)
{
    const float scaledHue = hue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaledHue)) % 6;
    const float fraction = scaledHue - std::floor(scaledHue);
    const float low = value * (1.0f - saturation);
    const float falling = value * (1.0f - saturation * fraction);
    const float rising = value * (1.0f - saturation * (1.0f - fraction));

    switch (sector)
    {
    case 0: return {value, rising, low};
    case 1: return {falling, value, low};
    case 2: return {low, value, rising};
    case 3: return {low, falling, value};
    case 4: return {rising, low, value};
    default: return {value, low, falling};
    }
}

std::vector<uint32_t> SelectedNodeIds(Assets::Scene& scene)
{
    std::vector<uint32_t> ids = scene.GetSelectedIds();
    if (ids.empty())
    {
        const uint32_t selectedId = scene.GetSelectedId();
        if (selectedId != static_cast<uint32_t>(-1))
        {
            ids.push_back(selectedId);
        }
    }
    return ids;
}

} // namespace

// should use 1em instead of 1px
constexpr float constTitlebarSize = 48;
constexpr float constTitlebarRightInfoWidth = 0;
constexpr float constModeRailWidth = 56;
constexpr float constModeRailButtonSize = 40;

float TitlebarSize = constTitlebarSize;
float TitlebarRightInfoWidth = constTitlebarRightInfoWidth;
float ModeRailWidth = constModeRailWidth;
float ModeRailButtonSize = constModeRailButtonSize;

static void UpdateUiScaledMetrics()
{
    float scale = 1.0f;

#if !ANDROID
    if (Vulkan::SwapChain::UiContentScale() < 1.0f)
    {
        scale *= 0.75f / Vulkan::SwapChain::UiContentScale();
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        const float fontSize = ImGui::GetFontSize();
        if (fontSize > 0.0f)
        {
            constexpr float referenceFontSize = 16.0f;
            scale *= fontSize / referenceFontSize;
        }
    }
#endif

    TitlebarSize = constTitlebarSize * scale;
    TitlebarRightInfoWidth = constTitlebarRightInfoWidth * scale;
    ModeRailWidth = constModeRailWidth * scale;
    ModeRailButtonSize = constModeRailButtonSize * scale;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.HideTitleBar = true;
}

void NextRendererGameInstance::OnInit()
{
    // Keep the viewport clean on startup. The Stats button remains available in
    // the bottom status bar for sessions that need the diagnostic overlay.
    GetEngine().GetUserSettings().ShowOverlay = false;
    GetEngine().GetShowFlags().DebugCVarPanel = false;

    // First-run scene. CornellBox.proc is a near-square box that letterboxes badly on a
    // 16:9 window; playground fills the frame and shows GI, shadows and materials at once.
    std::string initializedScene = "assets/models/playground.glb";
    if (!GOption->SceneName.empty())
    {
        initializedScene = GOption->SceneName;
    }

    const std::filesystem::path initializedPath(initializedScene);
    for (int sceneIndex = 0;
         sceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size());
         ++sceneIndex)
    {
        const std::filesystem::path candidatePath(Runtime::Scene::SceneList::AllScenes[sceneIndex]);
        if (candidatePath == initializedPath || candidatePath.filename() == initializedPath.filename())
        {
            GetEngine().GetUserSettings().SceneIndex = sceneIndex;
            break;
        }
    }

    GetEngine().RequestLoadScene({.filename = initializedScene});

#if GK_WITH_VITURE
    if (GOption != nullptr && GOption->ArMode)
    {
        headPoseTracker_ = Modules::Viture::CreateHeadPoseTracker(
            GOption->ArDof == 6, static_cast<double>(GOption->ArPredictionMs) * 0.001);
        if (!headPoseTracker_->Start())
        {
            SPDLOG_ERROR("AR mode could not start {}: {}", headPoseTracker_->Name(), headPoseTracker_->Status());
        }
    }
#endif
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    bool arMoving = false;
#if GK_WITH_VITURE
    arMoving = UpdateArTracking(deltaSeconds);
#endif
    if (playbackPaused_ && !stepRequested_)
    {
        GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender && !arMoving);
        return;
    }

    const bool moving = modelViewController_.UpdateCamera(10.0f, deltaSeconds) || arMoving;
    GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender && !moving);
    stepRequested_ = false;
}

void NextRendererGameInstance::OnDestroy()
{
#if GK_WITH_VITURE
    if (headPoseTracker_)
    {
        headPoseTracker_->Stop();
    }
#endif
}

#if GK_WITH_VITURE
bool NextRendererGameInstance::UpdateArTracking(const double deltaSeconds)
{
    if (!headPoseTracker_)
    {
        return false;
    }

    const std::optional<Modules::Viture::FHeadPose> pose = headPoseTracker_->PollPose();
    latestArPose_ = pose;
    const float smoothingHz = GOption != nullptr ? GOption->ArSmoothingHz : 0.0f;
    return pose.has_value() && arCamera_.Update(*pose, deltaSeconds, smoothingHz);
}

void NextRendererGameInstance::DrawVitureDebugPanel()
{
    if (!headPoseTracker_)
    {
        return;
    }

    const float worldUnitsPerMeter = GOption != nullptr ? GOption->ArWorldUnitsPerMeter : 1.0f;
    const float predictionMs = GOption != nullptr ? GOption->ArPredictionMs : 20.0f;
    const float smoothingHz = GOption != nullptr ? GOption->ArSmoothingHz : 0.0f;
    DevTools::FVitureDebugPanelData data{};
    data.tracker = headPoseTracker_.get();
    data.pose = latestArPose_.has_value() ? &latestArPose_.value() : nullptr;
    const std::optional<glm::quat> relativeOrientation = arCamera_.RelativeOrientation();
    const std::optional<glm::vec3> cameraEulerDegrees = latestArPose_.has_value() && relativeOrientation.has_value()
        ? std::optional<glm::vec3>(glm::degrees(glm::eulerAngles(*relativeOrientation)))
        : std::nullopt;
    data.cameraEulerDegrees = cameraEulerDegrees.has_value() ? &cameraEulerDegrees.value() : nullptr;
    data.sixDof = GOption == nullptr || GOption->ArDof == 6;
    data.worldUnitsPerMeter = worldUnitsPerMeter;
    data.predictionMs = predictionMs;
    data.pollHz = 25.0f;
    data.smoothingHz = smoothingHz;
    data.recenter = [this]() { return arCamera_.Recenter(); };
    data.restart = [this]()
    {
        latestArPose_.reset();
        arCamera_ = {};
        const bool started = headPoseTracker_ != nullptr && headPoseTracker_->Start();
        if (!started && headPoseTracker_ != nullptr)
        {
            SPDLOG_ERROR("AR mode could not restart {}: {}", headPoseTracker_->Name(), headPoseTracker_->Status());
        }
        return started;
    };
    DevTools::DrawVitureDebugPanel(vitureDebugPanelVisible_, data, GetGraphicsDebugPanelTopOffset());
}
#endif

std::vector<Assets::FMaterial> MatPreparedForAdd;

void NextRendererGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0,0,0), 0.2f));
    modelId_ = static_cast<uint32_t>(models.size() - 1);
    
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.2,-0.2,-0.2), glm::vec3(0.2,0.2,0.2)));
    boxModelId_ = static_cast<uint32_t>(models.size() - 1);

    matIds_.clear();

    matIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1,1,1)));
    MatPreparedForAdd.push_back(materials.back());
    MatPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.8,0.8,0.8), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));

    dropSphereMatIds_.clear();
    dropSphereMatIds_.reserve(dropSphereCount);
    dropSphereSequence_ = 0;
    for (uint32_t index = 0; index < dropSphereCount; ++index)
    {
        const float hue = HashToUnitFloat(index ^ 0xa511e9b3u);
        const float saturation = 0.25f + 0.25f * HashToUnitFloat(index ^ 0x63d83595u);
        const float value = 0.55f + 0.43f * HashToUnitFloat(index ^ 0xc2b2ae35u);
        const float roughness = 0.04f + 0.92f * HashToUnitFloat(index ^ 0x27d4eb2fu);
        const glm::vec3 color = HsvToRgb(hue, saturation, value);

        Assets::Material material;
        switch (HashUint(index ^ 0x9e3779b9u) % 5)
        {
        case 0: material = Assets::Material::Lambertian(color); break;
        case 1: material = Assets::Material::Metallic(color, roughness); break;
        case 3: material = Assets::Material::Dielectric(1.5f, 0.0f); break;
        default: material = Assets::Material::Mixture(color, roughness); break;
        }

        materials.push_back({material, fmt::format("dropSphere_{:03}", index)});
        dropSphereMatIds_.push_back(static_cast<uint32_t>(materials.size() - 1));
    }
}

void NextRendererGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    modelViewController_.Reset( GetEngine().GetScene().GetRenderCamera() );

    GetEngine().GetScene().PlayAllTracks();
}

void NextRendererGameInstance::OnPreConfigUI()
{
    NextGameInstanceBase::OnPreConfigUI();
}

bool NextRendererGameInstance::OnRenderUI()
{
    FGameUiFrameContext context;
    const auto& swapChain = GetEngine().GetRenderer().SwapChain();
    context.surfaceKind = FGameUiFrameContext::ESurfaceKind::MainWindow;
    context.framebufferExtent = swapChain.OutputExtent();
    context.viewCamera = &GetEngine().GetScene().GetRenderCamera();
    context.allowWindowCommands = true;
    return DrawRendererUi(context, mainUiState_);
}

bool NextRendererGameInstance::OnRenderUI(const FGameUiFrameContext& context)
{
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::RemoteView)
    {
        return DrawRendererUi(context, GetRemoteUiState(context.sessionId));
    }
    return OnRenderUI();
}

NextUI::FUiFrameResult NextRendererGameInstance::RenderUiFrame(const FGameUiFrameContext& context)
{
    return NextUI::FUiFrameResult::FromLegacyHandled(OnRenderUI(context));
}

void NextRendererGameInstance::OnRemoteUiSessionClosed(std::string_view sessionId)
{
    remoteUiStates_.erase(std::string(sessionId));
}

NextRendererGameInstance::FRendererUiState& NextRendererGameInstance::GetRemoteUiState(std::string_view sessionId)
{
    return remoteUiStates_[std::string(sessionId)];
}

bool NextRendererGameInstance::DrawRendererUi(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    if ((isTakingScreenshot_ || isRecordingVideo_) &&
        context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        return true;
    }

    UpdateUiScaledMetrics();

    if (uiState.workMode != uiState.lastWorkMode)
    {
        auto& showFlags = GetEngine().GetShowFlags();
        Runtime::Config::UserSettings& userSettings = GetEngine().GetUserSettings();
        switch (uiState.workMode)
        {
        case EWorkMode::Render:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Detail:
            uiState.showSettings = true;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Profile:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(true);
            userSettings.ShowOverlay = true;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::CVar:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = true;
            break;
        default: break;
        }
        uiState.lastWorkMode = uiState.workMode;
    }
    else if (uiState.workMode == EWorkMode::CVar &&
             !GetEngine().GetShowFlags().DebugCVarPanel)
    {
        uiState.workMode = EWorkMode::Render;
        uiState.lastWorkMode = EWorkMode::Count;
    }

    DrawTitleBar(context, uiState);
#if GK_WITH_VITURE
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        DrawVitureDebugPanel();
    }
#endif
    DrawModeRail(uiState);
    DrawSettings(uiState);
    DrawViewportTopBar(context, uiState);
    DrawViewportCheatSheet(uiState);
    DrawBottomStatusBar();
    Utilities::UI::ShowAboutDialog(uiState.showAbout);

    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow && ImGui::GetCurrentContext() != nullptr)
    {
        auto& swapChain = GetEngine().GetRenderer().SwapChain();
        const auto offset = swapChain.OutputOffset();
        const auto extent = swapChain.OutputExtent();
        const NextUI::Scaling::FViewportRect viewport = NextUI::Scaling::MainFramebufferToImGuiViewport(
            ImVec2(static_cast<float>(offset.x), static_cast<float>(offset.y)),
            ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
        uiState.gizmoController.Draw(GetEngine(),
            glm::vec2(viewport.Position.x, viewport.Position.y),
            glm::vec2(viewport.Size.x, viewport.Size.y));
    }
    if (GOption->ReferenceMode)
    {
        ImGuiIO& io = ImGui::GetIO();
        const auto viewport = io.DisplaySize;
        static constexpr std::array<const char*, 4> rendererNames{
            "SoftwareModern", "SoftwareTracing", "SoftwareModernNoAmbient", "PathTracing"};
        const std::array<ImVec2, rendererNames.size()> labelPositions{
            ImVec2(viewport.x * 0.25f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.25f, viewport.y * 0.95f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.95f)
        };
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoFocusOnAppearing;

        for (size_t index = 0; index < rendererNames.size(); ++index)
        {
            ImGui::SetNextWindowPos(labelPositions[index], ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.5f);

            auto windowName = fmt::format("RendererName{}", index);
            if (ImGui::Begin(windowName.c_str(), nullptr, windowFlags))
            {
                ImGui::TextUnformatted(rendererNames[index]);
            }
            ImGui::End();
        }
    }
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
}

void NextRendererGameInstance::RequestScreenshot(bool openFolder, const std::string& tag)
{
    if (isTakingScreenshot_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isTakingScreenshot_ = true;
    Runtime::IScreenShotService::FRequest request;
    request.tag = tag;
    request.onCompleted = [this, openFolder](const std::string&) {
        if (openFolder)
        {
            const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
            GetEngine().GetScreenShotService().EnsureDirectory();
            NextRenderer::OSCommand(folderPath.c_str());
        }
        isTakingScreenshot_ = false;
    };

    if (!GetEngine().GetScreenShotService().Request(std::move(request)))
    {
        isTakingScreenshot_ = false;
    }
}

void NextRendererGameInstance::RequestThreeSecondVideo(
    const Runtime::IScreenShotService::EVideoOutputScale outputScale)
{
    if (isTakingScreenshot_ || isRecordingVideo_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isRecordingVideo_ = true;
    Runtime::IScreenShotService::FThreeSecondVideoRequest request;
    // Without ffmpeg only the libwebp path can produce output; asking for Both would
    // just log an encoding error for the GIF half.
    request.format = GetEngine().GetScreenShotService().IsGifEncodingAvailable()
        ? Runtime::IScreenShotService::EAnimationFormat::Both
        : Runtime::IScreenShotService::EAnimationFormat::AnimatedWebp;
    request.outputScale = outputScale;
    request.onCaptureFinished = [this]()
    {
        isRecordingVideo_ = false;
    };
    request.onCompleted = [](const std::string& path)
    {
        if (path.empty())
        {
            spdlog::error("Three-second video recording failed");
        }
        else
        {
            std::filesystem::path webpPath(path);
            webpPath.replace_extension(".webp");
            spdlog::info("Three-second GIF/WebP saved: {} and {}", path, webpPath.string());
        }
    };

    if (!GetEngine().GetScreenShotService().RequestThreeSecondVideo(std::move(request)))
    {
        isRecordingVideo_ = false;
    }
}

void NextRendererGameInstance::DrawVideoCaptureMenuItems()
{
    const auto outputScaleLabel = [](const Runtime::IScreenShotService::EVideoOutputScale outputScale)
    {
        switch (outputScale)
        {
        case Runtime::IScreenShotService::EVideoOutputScale::Half:
            return "50% Swapchain";
        case Runtime::IScreenShotService::EVideoOutputScale::Quarter:
            return "25% Swapchain";
        case Runtime::IScreenShotService::EVideoOutputScale::Full:
        default:
            return "100% Swapchain";
        }
    };

    const std::string outputScaleMenuLabel = fmt::format(
        "Recording size ({})", outputScaleLabel(videoOutputScale_));
    if (ImGui::BeginMenu(outputScaleMenuLabel.c_str()))
    {
        struct FVideoOutputScaleOption
        {
            Runtime::IScreenShotService::EVideoOutputScale scale;
            const char* label;
        };
        static constexpr std::array<FVideoOutputScaleOption, 3> options{{
            {Runtime::IScreenShotService::EVideoOutputScale::Full, "100% Swapchain"},
            {Runtime::IScreenShotService::EVideoOutputScale::Half, "50% Swapchain"},
            {Runtime::IScreenShotService::EVideoOutputScale::Quarter, "25% Swapchain"},
        }};

        for (const FVideoOutputScaleOption& option : options)
        {
            if (ImGui::MenuItem(option.label, nullptr, videoOutputScale_ == option.scale))
            {
                videoOutputScale_ = option.scale;
            }
        }
        ImGui::EndMenu();
    }

    // GIF encoding needs ffmpeg next to the executable, which release packages do not
    // ship. Say so in the menu instead of failing silently into the log.
    const bool gifAvailable = GetEngine().GetScreenShotService().IsGifEncodingAvailable();
    if (ImGui::MenuItem(gifAvailable ? "Record 3s GIF + Animated WebP" : "Record 3s Animated WebP"))
    {
        RequestThreeSecondVideo(videoOutputScale_);
    }
    if (!gifAvailable && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("GIF output is unavailable: ffmpeg was not found next to the executable.");
    }
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
    outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
#if GK_WITH_VITURE
    if (headPoseTracker_ && GOption != nullptr)
    {
        outRenderCamera.ModelView = arCamera_.BuildModelView(
            outRenderCamera.ModelView, GOption->ArWorldUnitsPerMeter);
    }
#endif
    return true;
}

float NextRendererGameInstance::GetGraphicsDebugPanelTopOffset() const
{
    return TitlebarSize;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE camera movement (only active when right mouse is pressed)
    modelViewController_.OnKey(event);

    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
#if GK_WITH_VITURE
        if (event.key.key == SDLK_R && headPoseTracker_ && arCamera_.Recenter())
        {
            SPDLOG_INFO("VITURE AR: tracking origin recentered");
            return true;
        }
#endif

        switch (event.key.key)
        {
        case SDLK_ESCAPE:
            GetEngine().GetScene().SetSelectedId(-1);
            GetEngine().GetShowFlags().ShowEdge = false;
            return true;
        case SDLK_F:
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            break;
        case SDLK_SPACE: CreateBoxAndPush(); return true;
        case SDLK_B: DropPhysicsSphereGrid(); return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
        {
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DeleteNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        case SDLK_D:
        {
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) break;
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DuplicateNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        default: break;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        switch (event.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            CreateSphereAndPush(); return true;
            break;
        default: break;
        }
    }
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    // Update Controller Context
    bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    modelViewController_.SetAltPressed(alt);
    
    glm::vec3 center;
    float radius;
    if (GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        modelViewController_.SetOrbitTarget(center);
    }
    else
    {
        modelViewController_.SetOrbitTarget(std::nullopt);
    }

    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnCursorPosition(xpos, ypos);
    }
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnMouseButton(event);
    }
    else
    {
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCast( org, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hit)
            {
                GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                Runtime::EngineHelper::DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
                GetEngine().GetScene().SetSelectedId(result.InstanceId);
                GetEngine().GetShowFlags().ShowEdge = true;
            }
            else
            {
                GetEngine().GetScene().SetSelectedId(-1);
                GetEngine().GetShowFlags().ShowEdge = false;
            }
            return true;
        });
        return true;
    }

    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnScroll(xoffset, yoffset);
    }
    return true;
}

bool NextRendererGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
    int16_t leftTrigger, int16_t rightTrigger)
{
    return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}

bool NextRendererGameInstance::OnRemoteViewAction(const FRemoteViewActionContext& context, std::string_view action)
{
    if (action != "space")
    {
        return false;
    }

    const std::string shortSession = context.sessionId.substr(0, std::min<size_t>(context.sessionId.size(), 8));
    CreateBoxAndPushFromView(FLaunchView{
        .position = context.position,
        .forward = context.forward,
        .right = context.right,
        .up = context.up,
        .debugName = fmt::format("remoteBox-{}", shortSession.empty() ? "client" : shortSession),
    });
    return true;
}

void NextRendererGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    //std::string error;
    //cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
    glm::vec3 forward = modelViewController_.GetForward();
    glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
    glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
    glm::vec3 shotDir = normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode = Assets::SceneBuilder::CreateRenderNode("temp", center, glm::vec3(1), instanceId, modelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateSphereBody(center, 0.2f, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::CreateBoxAndPush()
{
    CreateBoxAndPushFromView(FLaunchView{
        .position = modelViewController_.GetPosition(),
        .forward = modelViewController_.GetForward(),
        .right = modelViewController_.GetRight(),
        .up = modelViewController_.GetUp(),
        .debugName = "tempBox",
    });
}

void NextRendererGameInstance::CreateBoxAndPushFromView(const FLaunchView& view)
{
    if (matIds_.empty())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic materials are ready");
        return;
    }
    if (boxModelId_ >= GetEngine().GetScene().Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic box model is ready");
        return;
    }

    glm::vec3 forward = glm::normalize(view.forward);
    glm::vec3 right = glm::normalize(view.right);
    glm::vec3 up = glm::normalize(view.up);
    glm::vec3 center = view.position + forward * 0.1f + right * 0.5f + up * -0.5f;
    glm::vec3 farTarget = view.position + forward * 1000.0f + up * 200.f;
    glm::vec3 shotDir = glm::normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode =
        Assets::SceneBuilder::CreateRenderNode(view.debugName, center, glm::vec3(1), instanceId, boxModelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateBoxBody(center, {0.4,0.4,0.4}, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 100000.f);
}

void NextRendererGameInstance::DropPhysicsSphereGrid()
{
    Assets::Scene& scene = GetEngine().GetScene();
    NextPhysics* physicsEngine = GetEngine().GetPhysicsEngine();
    if (physicsEngine == nullptr)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop because physics is unavailable");
        return;
    }
    if (modelId_ >= scene.Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before dynamic sphere model is ready");
        return;
    }
    if (dropSphereMatIds_.size() != dropSphereCount)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before sphere materials are ready");
        return;
    }

    const glm::vec3 boundsMin = scene.GetSceneAABBMin() * 0.5f;
    const glm::vec3 boundsMax = scene.GetSceneAABBMax() * 0.5f;
    const glm::vec3 boundsSize = glm::max(boundsMax - boundsMin, glm::vec3(0.0f));
    const float fallbackSpan = std::max({boundsSize.x, boundsSize.y * 0.5f, boundsSize.z, 4.0f});
    const float spanX = boundsSize.x > 0.01f ? boundsSize.x : fallbackSpan;
    const float spanZ = boundsSize.z > 0.01f ? boundsSize.z : fallbackSpan;
    const float stepX = spanX / static_cast<float>(dropSphereGridSize);
    const float stepZ = spanZ / static_cast<float>(dropSphereGridSize);
    const float radius = std::max(0.05f, std::min(stepX, stepZ) * 0.3f);
    const float renderScale = radius / 0.2f;
    const float startX = boundsSize.x > 0.01f ? boundsMin.x : (boundsMin.x + boundsMax.x - spanX) * 0.5f;
    const float startZ = boundsSize.z > 0.01f ? boundsMin.z : (boundsMin.z + boundsMax.z - spanZ) * 0.5f;
    const float spawnY = boundsMax.y + radius + std::max(0.05f, boundsSize.y * 0.02f);
    std::vector<uint32_t> shuffledMaterialIds = dropSphereMatIds_;
    std::mt19937 randomEngine(HashUint(0x6d2b79f5u ^ dropSphereSequence_++));
    std::shuffle(shuffledMaterialIds.begin(), shuffledMaterialIds.end(), randomEngine);

    for (uint32_t row = 0; row < dropSphereGridSize; ++row)
    {
        for (uint32_t column = 0; column < dropSphereGridSize; ++column)
        {
            const uint32_t sphereIndex = row * dropSphereGridSize + column;
            const glm::vec3 position{
                startX + (static_cast<float>(column) + 0.5f) * stepX,
                spawnY,
                startZ + (static_cast<float>(row) + 0.5f) * stepZ,
            };
            const uint32_t instanceId = static_cast<uint32_t>(scene.Nodes().size());
            std::shared_ptr<Assets::Node> node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("dropSphere_{:03}", sphereIndex),
                position,
                glm::vec3(renderScale),
                instanceId,
                modelId_,
                shuffledMaterialIds[sphereIndex]);

            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            physics->BindPhysicsBody(
                physicsEngine->CreateSphereBody(position, radius, NextMotionType::Dynamic));
            node->AddComponent(physics);
            scene.AddNode(std::move(node));
        }
    }

    scene.MarkDirty();
    SPDLOG_INFO("gkNextRenderer: dropped {} physics spheres above scene bounds", dropSphereCount);
}
