#include "EditorMain.h"
#include <Engine/Runtime/Platform/PlatformCommon.hpp>
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "EditorInterface.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/RenderViews/OffscreenRenderViewController.hpp"
#include "Modules/SceneExport/FSceneSaver.h"

#include "EditorActionDispatcher.hpp"
#include "EditorContext.hpp"
#include "Core/RecentScenes.hpp"
#include "Core/SceneSavePolicy.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/SceneContent/SceneList.hpp"

#include <spdlog/spdlog.h>

#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#if GK_WITH_VITURE
#include "Modules/DevTools/VitureDebugPanel.hpp"
#include "Modules/NextViture/VitureModule.hpp"
#endif
#include "Application/Common/DemoScenes.hpp"
#include "Gameplay/Rig/RigSubsystem.h"
#include "Application/Editor/Common/MultiViewportBackend.hpp"

#include <algorithm>
#include <cfloat>
#include <cctype>

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    // ScadRig characters, so a played-in-editor game that has them behaves as it does in the
    // launcher. Costs nothing for a scene without characters.
    if (engine != nullptr)
    {
        NextGameplay::Rig::Install(*engine);
    }
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine),
    playSession_(*engine)
{
    editorUserInterface_ = std::make_unique<EditorInterface>(this);

    NextRenderer::HideConsole();

    // windows config
    config.Title = "gkNextEditor";
    if (config.HeadlessSurface || config.HiddenWindow)
    {
        // A headless host has no desktop work area. SDL's fallback display can
        // report a virtual 16K monitor, which would make the editor allocate
        // unreasonably large render targets. Keep the requested CLI size.
        //
        // A hidden window gets the same treatment: nobody is looking at it, and sizing it from the
        // monitor makes agent validation depend on the machine's display. Honouring the requested
        // size is what lets a script address the UI in normalized coordinates at all.
        config.Width = options.Width;
        config.Height = options.Height;
    }
    else
    {
        glm::ivec2 monitorSize = GetEngine().GetMonitorSize();
        uint32_t computedWidth = static_cast<uint32_t>(monitorSize.x * 0.75f);
        uint32_t computedHeight = static_cast<uint32_t>(monitorSize.y * 0.75f);
        config.Width = computedWidth < 1920u ? static_cast<uint32_t>(monitorSize.x) : computedWidth;
        config.Height = computedHeight < 1080u ? static_cast<uint32_t>(monitorSize.y) : computedHeight;
    }
    options.Width = config.Width;
    options.Height = config.Height;
    config.HideTitleBar = true;
    options.KeepCPUMeshData = true; // 编辑器模式保留CPU网格数据用于场景保存
    options.HighPrecisionProgressiveHistory = true;
}

std::unique_ptr<NextUI::IMultiViewportBackend> EditorGameInstance::CreateMultiViewportBackend()
{
    return std::make_unique<NextUI::MultiViewportBackend>(GetEngine());
}

void EditorGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    //cvars.SetDefaultFromString("r.samples", "4", &error);
    //cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
    cvars.SetDefaultFromString("r.progressiveRender", "true", &error);
    cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
    cvars.RegisterBool("ed.hoverHighlight", true, &settings_.hoverHighlight, NextCVar::ECVarFlags::Archive,
                       "Raycast under the cursor and highlight the hovered object");
    cvars.RegisterBool("ed.outlinerAutoScroll", true, &settings_.outlinerAutoScroll, NextCVar::ECVarFlags::Archive,
                       "Automatically scroll the Outliner to the selected object");
    cvars.RegisterBool("ed.gizmoSnap", false, &settings_.gizmoSnap, NextCVar::ECVarFlags::Archive,
                       "Enable transform gizmo snapping");
    cvars.RegisterFloat("ed.gizmoSnapTranslate", 1.0f, &settings_.gizmoSnapTranslate,
                        NextCVar::ECVarFlags::Archive, "Translation gizmo snap distance", nullptr, 0.001, 1000.0);
    cvars.RegisterInt("ed.gizmoDefaultMode", 0, &settings_.gizmoDefaultMode, NextCVar::ECVarFlags::Archive,
                      "Default gizmo operation (0=translate,1=rotate,2=scale)", nullptr, 0, 2);
    cvars.RegisterInt("ed.progressiveRenderResumeFrames", 8, &settings_.progressiveRenderResumeFrames,
                      NextCVar::ECVarFlags::Archive,
                      "Frames to wait after gizmo interaction before resuming progressive rendering", nullptr, 0, 120);
    cvars.RegisterUserFileChannel("ed.", "assets/configs/cvar_user.editor.json");

    // Play-in-editor control channel. Not archived: which game was running is a session fact, not
    // a preference. Both callbacks only record the request — starting a game reloads the world,
    // which must not happen inside a cvar write.
    cvars.RegisterString("ed.play", "", &playCVarValue_, NextCVar::ECVarFlags::None,
                         "Managed game to run in the editor: a game id from assets/configs/games, "
                         "or empty to stop",
                         [this]()
                         {
                             pendingPlayRequest_ = playCVarValue_;
                             hasPendingPlayRequest_ = true;
                         });
    cvars.RegisterBool("ed.playEject", false, &playEjectCVar_, NextCVar::ECVarFlags::None,
                       "While playing: take input and the camera back from the game",
                       [this]() { playSession_.SetEjected(playEjectCVar_); });
    cvars.RegisterBool("ed.newProject", false, &newProjectCVar_, NextCVar::ECVarFlags::None,
                       "Open the new game project dialog",
                       [this]()
                       {
                           if (newProjectCVar_)
                           {
                               playSession_.OpenNewProjectDialog();
                           }
                       });
}

void EditorGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("pieState", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                switch (playSession_.State())
                {
                case Editor::EPlayState::Playing: return std::string("Playing");
                case Editor::EPlayState::Ejected: return std::string("Ejected");
                default: return std::string("Stopped");
                }
            });
    reg.Add("pieActive", [this]() -> Runtime::Agent::FAgentQueryValue { return playSession_.ActiveGameId(); });
    reg.Add("pieAvailable", [this]() -> Runtime::Agent::FAgentQueryValue { return playSession_.IsAvailable(); });
    reg.Add("pieGameCount", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(playSession_.Games().size()); });
    reg.Add("newProjectOpen", [this]() -> Runtime::Agent::FAgentQueryValue
            { return playSession_.IsNewProjectDialogOpen(); });
    reg.Add("canCreateProject", [this]() -> Runtime::Agent::FAgentQueryValue
            { return playSession_.CanCreateProject(); });
    reg.Add("scenePath", [this]() -> Runtime::Agent::FAgentQueryValue
            { return GetEditorInterface().GetEditorUiState().currentScenePath; });
    reg.Add("selectedId", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(GetEngine().GetScene().GetSelectedId()); });
}

void EditorGameInstance::OnInit()
{
    actions_.RegisterAction(EEditorAction::System_RequestExit,
                            [](EditorContext& ctx, std::string_view /*args*/) -> bool
                            {
                                ctx.engine.RequestClose();
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::System_ToggleMaximize,
                            [](EditorContext& ctx, std::string_view /*args*/) -> bool
                            {
                                ctx.engine.ToggleMaximize();
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::System_RequestMinimize,
                            [](EditorContext& ctx, std::string_view /*args*/) -> bool
                            {
                                ctx.engine.RequestMinimize();
                                return true;
                            });

    // Scene switching invalidates undo/redo history.
    actions_.RegisterAction(EEditorAction::IO_LoadScene,
                            [this](EditorContext& ctx, std::string_view args) -> bool
                            {
                                ctx.engine.GetCommandHistory().Clear();
                                const std::string scenePath(args);
                                ctx.engine.RequestLoadScene({.filename = scenePath});
                                GetEditorInterface().GetEditorUiState().currentScenePath = scenePath;
                                PushRecentScene(GetEditorInterface().GetEditorUiState(), scenePath);
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::IO_LoadSceneAdd,
                            [this](EditorContext& ctx, std::string_view args) -> bool
                            {
                                ctx.engine.GetCommandHistory().Clear();
                                const std::string scenePath(args);
                                ctx.engine.RequestLoadScene({.filename = scenePath, .append = true});
                                PushRecentScene(GetEditorInterface().GetEditorUiState(), scenePath);
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::IO_AddSceneReference,
                            [this](EditorContext& ctx, std::string_view args) -> bool
                            {
                                const std::string currentScenePath =
                                    GetEditorInterface().GetEditorUiState().currentScenePath;
                                if (!currentScenePath.empty())
                                {
                                    const std::string ext = std::filesystem::path(currentScenePath).extension().string();
                                    const std::string lowerExt = [&ext]()
                                    {
                                        std::string result = ext;
                                        std::transform(result.begin(), result.end(), result.begin(),
                                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                        return result;
                                    }();
                                    if (lowerExt != ".gltf" && lowerExt != ".glb")
                                    {
                                        SPDLOG_WARN("Scene references can only be authored in glTF/GLB host scenes");
                                        return false;
                                    }
                                }

                                ctx.engine.RequestAddSceneReference(std::string(args), glm::vec3(0.0f));
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::IO_LoadHDRI,
                            [](EditorContext& ctx, std::string_view args) -> bool
                            {
                                const std::string filename(args);
                                const bool hasMountedEntry =
                                    Utilities::Package::FPackageFileSystem::GetInstance().HasMountedEntry(filename);
                                std::error_code existsError;
                                const bool hasFilesystemEntry = std::filesystem::exists(filename, existsError) ||
                                    std::filesystem::exists(Utilities::FileHelper::GetRuntimeFilePath(filename), existsError);
                                if (!hasMountedEntry && !hasFilesystemEntry)
                                {
                                    SPDLOG_ERROR("Failed to load HDRI: {}", filename);
                                    return false;
                                }

                                const uint32_t textureId = Assets::GlobalTexturePool::GetInstance()->LoadHDRTexture(filename);
                                ctx.scene.GetEnvSettings().SkyIdx = static_cast<int32_t>(textureId);
                                SPDLOG_INFO("HDRI loaded: {} (SkyIdx={})", filename, textureId);
                                return true;
                            });

    actions_.RegisterAction(EEditorAction::Camera_FocusSelected,
                            [this](EditorContext& ctx, std::string_view args) -> bool
                            {
                                glm::vec3 center;
                                float radius;
                                bool found = args.empty()
                                    ? ctx.scene.GetSelectedNodeBounds(center, radius)
                                    : ctx.scene.GetNodeBounds(static_cast<uint32_t>(std::stoul(std::string(args))), center, radius);

                                if (found)
                                {
                                    ControllerForViewport(ActiveViewportFromUi()).Focus(center, radius);
                                }
                                return found;
                            });

    GetEngine().GetShowFlags().ShowEdge = true;
    modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
    for (auto& cameraViewController : cameraViewControllers_)
    {
        cameraViewController.Reset(GetEngine().GetScene().GetRenderCamera());
    }

    // Open the scene passed on the command line (--load-scene) so the editor can start directly on a
    // scene (and supports automated --agent-validation screenshots). Without a command line scene,
    // fall back to a sample so a first launch shows a populated viewport and outliner instead of a
    // black void with a lone Environment node.
    std::string startupScene = GOption != nullptr ? GOption->SceneName : std::string();
    if (startupScene.empty())
    {
        constexpr const char* defaultScene = "assets/models/playground.glb";
        if (Utilities::FileHelper::IsAssetAvailable(defaultScene))
        {
            startupScene = defaultScene;
        }
        else
        {
            SPDLOG_WARN("Default editor scene '{}' is unavailable; starting with an empty scene.", defaultScene);
        }
    }

    if (!startupScene.empty())
    {
        GetEngine().RequestLoadScene({.filename = startupScene});
        GetEditorInterface().GetEditorUiState().currentScenePath = startupScene;
    }

    playSession_.Initialize();

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

void EditorGameInstance::OnTick(double deltaSeconds)
{
    playSession_.OnTick(deltaSeconds);

    if (hasPendingPlayRequest_)
    {
        const std::string requested = pendingPlayRequest_;
        hasPendingPlayRequest_ = false;
        if (requested.empty())
        {
            playSession_.Stop();
        }
        else
        {
            GetEditorInterface().GetEditorUiState().lastPlayedGameId = requested;
            StartPlaySession(requested);
        }
    }

    if (playSession_.IsRunning())
    {
        // A running game animates the world every frame, so accumulating a progressive image would
        // only average together frames that no longer agree. The editor camera is also frozen while
        // the game owns input; ejecting hands it back.
        GetEngine().SetProgressiveRendering(false);
        if (!playSession_.GameOwnsInput())
        {
            modelViewController_.UpdateCamera(1.0f, deltaSeconds);
        }
        return;
    }

    const bool progressiveEnabled = GetEngine().GetUserSettings().ProgressiveRender;
    bool moving = modelViewController_.UpdateCamera(1.0f, deltaSeconds);
#if GK_WITH_VITURE
    moving |= UpdateArTracking(deltaSeconds);
#endif
    for (auto& cameraViewController : cameraViewControllers_)
    {
        moving |= cameraViewController.UpdateCamera(1.0f, deltaSeconds);
    }
    if (GOption != nullptr && GOption->RemoteMode && GOption->RemoteMultiView)
    {
        progressiveRenderResumeFramesRemaining_ =
            static_cast<uint32_t>(std::max(settings_.progressiveRenderResumeFrames, 0));
        GetEngine().SetProgressiveRendering(false);
        return;
    }

    const uint32_t progressiveRenderResumeFrames =
        static_cast<uint32_t>(std::max(settings_.progressiveRenderResumeFrames, 0));
    if (!progressiveEnabled || moving || gizmoController_.IsUsing())
    {
        progressiveRenderResumeFramesRemaining_ = progressiveRenderResumeFrames;
        GetEngine().SetProgressiveRendering(false);
        return;
    }

    if (progressiveRenderResumeFramesRemaining_ > 0)
    {
        --progressiveRenderResumeFramesRemaining_;
        GetEngine().SetProgressiveRendering(false);
        return;
    }

    GetEngine().SetProgressiveRendering(progressiveEnabled);
}

void EditorGameInstance::OnDestroy()
{
    playSession_.OnEditorDestroy();
#if GK_WITH_VITURE
    if (headPoseTracker_)
    {
        headPoseTracker_->Stop();
    }
#endif
}

#if GK_WITH_VITURE
bool EditorGameInstance::UpdateArTracking(const double deltaSeconds)
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

void EditorGameInstance::DrawVitureDebugPanel()
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
    DevTools::DrawVitureDebugPanel(vitureDebugPanelVisible_, data, 48.0f);
}
#endif

void EditorGameInstance::OnSceneLoaded()
{
    playSession_.OnSceneLoaded();
    modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
    for (auto& cameraViewController : cameraViewControllers_)
    {
        cameraViewController.Reset(GetEngine().GetScene().GetRenderCamera());
    }
}

void EditorGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                            std::vector<Assets::Model>& models,
                                            std::vector<Assets::FMaterial>& materials,
                                            std::vector<Assets::LightObject>& lights,
                                            std::vector<Assets::AnimationTrack>& tracks)
{
    // A managed game builds its world here. Without this the editor would load the game's scene
    // file and none of the content the game generates procedurally.
    playSession_.OnBeforeSceneRebuild(nodes, models, materials, lights, tracks);
}

bool EditorGameInstance::OnGameRequestedClose()
{
    // A game quitting means "end the Play session", never "close the editor".
    return playSession_.OnGameRequestedClose();
}

bool EditorGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX,
                                        int16_t rightStickY, int16_t leftTrigger, int16_t rightTrigger)
{
    playSession_.SetGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
    return false;
}

void EditorGameInstance::StartPlaySession(const std::string& gameId)
{
    playSession_.Play(gameId, GetEditorInterface().GetEditorUiState().currentScenePath);
}

void EditorGameInstance::SelectSceneCamera(const size_t cameraIndex)
{
    auto& scene = GetEngine().GetScene();
    const auto& cameras = scene.GetEnvSettings().cameras;
    if (cameraIndex >= cameras.size())
    {
        return;
    }

    GetEngine().GetUserSettings().CameraIdx = static_cast<int>(cameraIndex);
    scene.GetRenderCamera() = cameras[cameraIndex];
    modelViewController_.Reset(scene.GetRenderCamera());
    GetEngine().ResetProgressiveRenderingAccumulation();
    GetEngine().GetRenderer().PrimaryView().InvalidateTemporalHistory(
        Vulkan::EHistoryInvalidationReason::CameraCut);
}

void EditorGameInstance::ResetToDefaultSceneCamera()
{
    SelectSceneCamera(0);
}

void EditorGameInstance::SetSceneViewportFieldOfView(const float fieldOfView)
{
    const float clampedFieldOfView = std::clamp(fieldOfView, 10.0f, 140.0f);
    GetEngine().GetScene().GetRenderCamera().FieldOfView = clampedFieldOfView;
    modelViewController_.SetFieldOfView(clampedFieldOfView);
    GetEngine().ResetProgressiveRenderingAccumulation();
    GetEngine().GetRenderer().PrimaryView().InvalidateTemporalHistory(
        Vulkan::EHistoryInvalidationReason::CameraCut);
}

void EditorGameInstance::OnPreConfigUI() { editorUserInterface_->Config(); }

bool EditorGameInstance::OnRenderUI()
{
    editorUserInterface_->Render();
#if GK_WITH_VITURE
    DrawVitureDebugPanel();
#endif
    // After the editor, so a game HUD sits on top of the viewport rather than under the panels.
    // Suppressed while ejected — see FPlaySession::OnRenderGameUI.
    RenderPlaySessionUI();
    return true;
}

void EditorGameInstance::RenderPlaySessionUI()
{
    // The game's screen is the viewport panel, not the editor window. Without this a HUD written
    // against a full window lands in the top-left corner of the editor and paints over the panels.
    const Editor::EditorUiState& ui = GetEditorInterface().GetEditorUiState();
    playSession_.OnRenderGameUI(ui.viewportContentPos.x, ui.viewportContentPos.y,
                                ui.viewportContentSize.x, ui.viewportContentSize.y);
}

bool EditorGameInstance::OnRenderUI(const FGameUiFrameContext& context)
{
    editorUserInterface_->Render(context);
#if GK_WITH_VITURE
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        DrawVitureDebugPanel();
    }
#endif
    return true;
}

NextUI::FUiFrameResult EditorGameInstance::RenderUiFrame(const FGameUiFrameContext& context)
{
    editorUserInterface_->Render(context);
#if GK_WITH_VITURE
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        DrawVitureDebugPanel();
    }
#endif
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        // Only on the main window: a remote view renders the same scene but must not receive a
        // second copy of the game's immediate-mode UI.
        RenderPlaySessionUI();
    }
    return {NextUI::EUiDeveloperLayer::All};
}

void EditorGameInstance::OnInitUI() { editorUserInterface_->Init(); }

void EditorGameInstance::OnRemoteUiSessionClosed(std::string_view sessionId)
{
    editorUserInterface_->OnRemoteUiSessionClosed(sessionId);
}

bool EditorGameInstance::OnKey(SDL_Event& event)
{
    // A modal owns the keyboard while it is up; F5 starting a game underneath it would be a
    // surprise, and Escape has to reach the dialog rather than the editor's own handling.
    if (playSession_.IsNewProjectDialogOpen())
    {
        return false;
    }

    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_F5:
            if (playSession_.IsRunning())
            {
                playSession_.Stop();
            }
            else if (!GetEditorInterface().GetEditorUiState().lastPlayedGameId.empty())
            {
                StartPlaySession(GetEditorInterface().GetEditorUiState().lastPlayedGameId);
            }
            return true;
        case SDLK_F8:
            playSession_.ToggleEject();
            return true;
        default:
            break;
        }
    }

    // While the game owns input the editor keeps its hands off: the engine has already delivered
    // this event to the managed side, and reacting here too would move the editor camera and fire
    // editor shortcuts underneath the running game.
    if (playSession_.GameOwnsInput())
    {
        return true;
    }

#if GK_WITH_VITURE
    if (event.key.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R && headPoseTracker_ && arCamera_.Recenter())
    {
        SPDLOG_INFO("VITURE AR: tracking origin recentered");
        return true;
    }
#endif

    // WASDQE camera movement (only active when right mouse is pressed)
    ControllerForViewport(ActiveViewportFromUi()).OnKey(event);

    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_ESCAPE:
            GetEngine().GetScene().ClearSelection();
            break;
        case SDLK_COMMA:
            if ((event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 && !ImGui::GetIO().WantTextInput)
            {
                auto& uiState = GetEditorInterface().GetEditorUiState();
                uiState.settingsPanel = !uiState.settingsPanel;
            }
            break;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
        {
            if (ImGui::GetIO().WantTextInput) break;
            std::vector<uint32_t> ids = GetEngine().GetScene().GetSelectedIds();
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DeleteNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            break;
        }
        case SDLK_D:
        {
            if (ImGui::GetIO().WantTextInput) break;
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) break;
            std::vector<uint32_t> ids = GetEngine().GetScene().GetSelectedIds();
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DuplicateNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            break;
        }
        case SDLK_S:
        {
            if (ImGui::GetIO().WantTextInput) break;
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) break;
            auto& ui = GetEditorInterface().GetEditorUiState();
            if (Editor::CanOverwriteCurrentScene(ui.currentScenePath))
            {
                const std::string savePath = Editor::ResolveSceneFilesystemPath(ui.currentScenePath).string();
                if (SceneExport::SaveScene(GetEngine().GetScene(), savePath))
                {
                    SPDLOG_INFO("Scene saved: {}", savePath);
                }
                else
                {
                    SPDLOG_ERROR("Failed to save scene: {}", savePath);
                }
            }
            else
            {
                SPDLOG_INFO("Current scene is not writable as GLB/glTF; use File > Save Scene As...");
            }
            break;
        }
        case SDLK_F:
            {
                // Focus on selected node (F key shortcut)
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    ControllerForViewport(ActiveViewportFromUi()).Focus(focusCenter, radius);
                }
            }
            break;
        default:
            break;
        }
    }
    return true;
}

bool EditorGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (playSession_.GameOwnsInput())
    {
        return true;
    }

    const auto hoverTarget = ResolveViewportUnderMouse();
    const bool rightMousePressed = (GetEngine().GetMouseButtons() & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    const std::optional<EViewportInputTarget> target =
        rightMousePressed && capturedInputViewport_.has_value() ? capturedInputViewport_ : hoverTarget;
    if (!target.has_value())
    {
        GetEngine().GetScene().ClearHoveredId();
        return true;
    }

    Runtime::Camera::ModelViewController& controller = ControllerForViewport(*target);

    // Update Controller Context
    UpdateControllerContext(controller);

    if (!gizmoController_.IsInteracting())
    {
        controller.OnCursorPosition(xpos, ypos);
    }

    if (settings_.hoverHighlight && !gizmoController_.IsInteracting() && !rightMousePressed)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mousePos = CameraViewIndex(*target).has_value() &&
                io.MousePos.x > -FLT_MAX * 0.5f && io.MousePos.y > -FLT_MAX * 0.5f
            ? glm::vec2(io.MousePos.x, io.MousePos.y)
            : glm::vec2(xpos, ypos);
        RayCastFromViewport(*target, mousePos,
                            [this](Assets::RayCastResult result)
                            {
                                if (result.Hit)
                                {
                                    GetEngine().GetScene().SetHoveredId(result.InstanceId);
                                }
                                else
                                {
                                    GetEngine().GetScene().ClearHoveredId();
                                }
                                return true;
                            });
    }
    else if (!settings_.hoverHighlight)
    {
        GetEngine().GetScene().ClearHoveredId();
    }

    return true;
}

bool EditorGameInstance::OnMouseButton(SDL_Event& event)
{
    if (playSession_.GameOwnsInput())
    {
        return true;
    }

    const auto targetUnderMouse = ResolveViewportUnderMouse();
    if (event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (!targetUnderMouse.has_value())
        {
            return true;
        }

        SetActiveInputViewport(*targetUnderMouse);
        capturedInputViewport_ = *targetUnderMouse;
    }

    const std::optional<EViewportInputTarget> eventTarget =
        event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            ? targetUnderMouse
            : (capturedInputViewport_.has_value() ? capturedInputViewport_ : targetUnderMouse);
    if (!eventTarget.has_value())
    {
        return true;
    }

    Runtime::Camera::ModelViewController& controller = ControllerForViewport(*eventTarget);
    UpdateControllerContext(controller);

    const bool releaseEvent = event.button.type == SDL_EVENT_MOUSE_BUTTON_UP;
    if (!gizmoController_.IsInteracting())
    {
        controller.OnMouseButton(event);
    }
    else
    {
        if (releaseEvent)
        {
            capturedInputViewport_.reset();
        }
        return true;
    }
    if (event.button.button == SDL_BUTTON_LEFT && event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        const bool toggleSelection = (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mousePos = CameraViewIndex(*eventTarget).has_value() &&
                io.MousePos.x > -FLT_MAX * 0.5f && io.MousePos.y > -FLT_MAX * 0.5f
            ? glm::vec2(io.MousePos.x, io.MousePos.y)
            : glm::vec2(event.button.x, event.button.y);
        RayCastFromViewport(*eventTarget, mousePos,
                            [this, toggleSelection](Assets::RayCastResult result)
                            {
                                if (result.Hit)
                                {
                                    if (GetEngine().GetScene().IsLocked(result.InstanceId))
                                    {
                                        return true;
                                    }
                                    GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                                    Runtime::EngineHelper::DrawAuxPoint(result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2,
                                                                   30);
                                    if (toggleSelection)
                                    {
                                        GetEngine().GetScene().ToggleSelection(result.InstanceId);
                                    }
                                    else
                                    {
                                        GetEngine().GetScene().SetSelectedId(result.InstanceId);
                                    }
                                }
                                else if (!toggleSelection)
                                {
                                    GetEngine().GetScene().ClearSelection();
                                }

                                return true;
                            });
        return true;
    }
    if (releaseEvent)
    {
        capturedInputViewport_.reset();
    }
    return true;
}

bool EditorGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (playSession_.GameOwnsInput())
    {
        return true;
    }

    const auto target = ResolveViewportUnderMouse();
    if (!target.has_value())
    {
        return true;
    }

    SetActiveInputViewport(*target);
    if (!gizmoController_.IsInteracting())
    {
        ControllerForViewport(activeInputViewport_).OnScroll(xoffset, yoffset);
    }
    return true;
}

Runtime::Camera::ModelViewController& EditorGameInstance::ControllerForViewport(EViewportInputTarget target)
{
    if (const std::optional<size_t> cameraViewIndex = CameraViewIndex(target))
    {
        return cameraViewControllers_[*cameraViewIndex];
    }
    return modelViewController_;
}

const Runtime::Camera::ModelViewController& EditorGameInstance::ControllerForViewport(EViewportInputTarget target) const
{
    if (const std::optional<size_t> cameraViewIndex = CameraViewIndex(target))
    {
        return cameraViewControllers_[*cameraViewIndex];
    }
    return modelViewController_;
}

std::optional<size_t> EditorGameInstance::CameraViewIndex(EViewportInputTarget target)
{
    switch (target)
    {
    case EViewportInputTarget::CameraView0:
        return 0;
    case EViewportInputTarget::CameraView1:
        return 1;
    case EViewportInputTarget::CameraView2:
        return 2;
    default:
        return std::nullopt;
    }
}

EditorGameInstance::EViewportInputTarget EditorGameInstance::CameraViewTarget(size_t viewIndex)
{
    switch (viewIndex)
    {
    case 0:
        return EViewportInputTarget::CameraView0;
    case 1:
        return EViewportInputTarget::CameraView1;
    default:
        return EViewportInputTarget::CameraView2;
    }
}

bool EditorGameInstance::IsMouseInRect(const glm::vec2& mousePos, const glm::vec2& rectPos,
                                       const glm::vec2& rectSize) const
{
    return rectSize.x > 1.0f && rectSize.y > 1.0f &&
        mousePos.x >= rectPos.x && mousePos.y >= rectPos.y &&
        mousePos.x < rectPos.x + rectSize.x && mousePos.y < rectPos.y + rectSize.y;
}

std::optional<EditorGameInstance::EViewportInputTarget> EditorGameInstance::ResolveViewportUnderMouse() const
{
    const auto& ui = GetEditorInterface().GetEditorUiState();
    glm::vec2 mousePos = glm::vec2(GetEngine().GetMousePos());
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MousePos.x > -FLT_MAX * 0.5f && io.MousePos.y > -FLT_MAX * 0.5f)
    {
        mousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
    }

    for (size_t i = 0; i < Editor::kMaxCameraViewports; ++i)
    {
        const auto& cameraView = ui.cameraViews[i];
        if (cameraView.open &&
            IsMouseInRect(mousePos,
                          glm::vec2(cameraView.contentPos.x, cameraView.contentPos.y),
                          glm::vec2(cameraView.contentSize.x, cameraView.contentSize.y)))
        {
            return CameraViewTarget(i);
        }
    }

    if (io.WantCaptureMouse)
    {
        return std::nullopt;
    }

    if (IsMouseInRect(mousePos,
                      glm::vec2(ui.viewportContentPos.x, ui.viewportContentPos.y),
                      glm::vec2(ui.viewportContentSize.x, ui.viewportContentSize.y)))
    {
        return EViewportInputTarget::Scene;
    }

    return std::nullopt;
}

EditorGameInstance::EViewportInputTarget EditorGameInstance::ActiveViewportFromUi() const
{
    const auto& ui = GetEditorInterface().GetEditorUiState();
    switch (ui.activeViewport)
    {
    case Editor::EEditorViewportId::CameraView0:
        return EViewportInputTarget::CameraView0;
    case Editor::EEditorViewportId::CameraView1:
        return EViewportInputTarget::CameraView1;
    case Editor::EEditorViewportId::CameraView2:
        return EViewportInputTarget::CameraView2;
    default:
        return EViewportInputTarget::Scene;
    }
}

void EditorGameInstance::SetActiveInputViewport(EViewportInputTarget target)
{
    activeInputViewport_ = target;
    auto& ui = GetEditorInterface().GetEditorUiState();
    switch (target)
    {
    case EViewportInputTarget::CameraView0:
        ui.activeViewport = Editor::EEditorViewportId::CameraView0;
        break;
    case EViewportInputTarget::CameraView1:
        ui.activeViewport = Editor::EEditorViewportId::CameraView1;
        break;
    case EViewportInputTarget::CameraView2:
        ui.activeViewport = Editor::EEditorViewportId::CameraView2;
        break;
    default:
        ui.activeViewport = Editor::EEditorViewportId::Scene;
        break;
    }
}

void EditorGameInstance::UpdateControllerContext(Runtime::Camera::ModelViewController& controller)
{
    controller.SetAltPressed((SDL_GetModState() & SDL_KMOD_ALT) != 0);

    glm::vec3 center;
    float radius;
    if (GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        controller.SetOrbitTarget(center);
    }
    else
    {
        controller.SetOrbitTarget(std::nullopt);
    }
}

Assets::Camera EditorGameInstance::BuildSceneViewportCamera() const
{
    Assets::Camera camera = GetEngine().GetScene().GetRenderCamera();
    camera.ModelView = modelViewController_.ModelView();
    camera.FieldOfView = modelViewController_.FieldOfView();
#if GK_WITH_VITURE
    if (headPoseTracker_ && GOption != nullptr)
    {
        camera.ModelView = arCamera_.BuildModelView(camera.ModelView, GOption->ArWorldUnitsPerMeter);
    }
#endif
    return camera;
}

Assets::Camera EditorGameInstance::BuildCameraViewCamera(size_t viewIndex) const
{
    viewIndex = std::min(viewIndex, cameraViewControllers_.size() - 1);
    Assets::Camera camera = GetEngine().GetScene().GetRenderCamera();
    camera.ModelView = cameraViewControllers_[viewIndex].ModelView();
    camera.FieldOfView = cameraViewControllers_[viewIndex].FieldOfView();
    return camera;
}

void EditorGameInstance::SyncCameraViewRendererCamera(size_t viewIndex, const glm::vec2& viewportSize)
{
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
    {
        return;
    }

    viewIndex = std::min(viewIndex, cameraViewControllers_.size() - 1);
    RenderViews::OffscreenViews(GetEngine().GetRenderer()).SetCameraOverride(
        static_cast<uint32_t>(viewIndex), BuildCameraViewCamera(viewIndex));
}

void EditorGameInstance::RayCastFromViewport(EViewportInputTarget target, const glm::vec2& mousePos,
                                             std::function<bool(Assets::RayCastResult)> callback)
{
    glm::vec3 org;
    glm::vec3 dir;
    if (const std::optional<size_t> cameraViewIndex = CameraViewIndex(target))
    {
        const auto& ui = GetEditorInterface().GetEditorUiState();
        const auto& cameraView = ui.cameraViews[*cameraViewIndex];
        Runtime::EngineHelper::GetScreenToWorldRayWithCamera(
            BuildCameraViewCamera(*cameraViewIndex),
            mousePos,
            glm::vec2(cameraView.contentPos.x, cameraView.contentPos.y),
            glm::vec2(cameraView.contentSize.x, cameraView.contentSize.y),
            org,
            dir);
    }
    else
    {
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
    }

    GetEngine().RayCast(org, dir, std::move(callback));
}

void EditorGameInstance::DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize)
{
    gizmoController_.Draw(GetEngine(), viewportPos, viewportSize, settings_.gizmoSnap,
                          settings_.gizmoSnapTranslate, settings_.gizmoDefaultMode);
}

void EditorGameInstance::DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize,
                                   const Assets::UniformBufferObject* viewUbo, ImGuiWindow* alternativeWindow)
{
    gizmoController_.Draw(GetEngine(), viewportPos, viewportSize, settings_.gizmoSnap,
                          settings_.gizmoSnapTranslate, settings_.gizmoDefaultMode, viewUbo, alternativeWindow);
}
