#include "EditorMain.h"
#include <Engine/Runtime/Platform/PlatformCommon.h>
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/GPU/Texture.hpp"
#include "EditorInterface.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"

#include "EditorActionDispatcher.hpp"
#include "EditorContext.hpp"
#include "Core/RecentScenes.hpp"
#include "Core/SceneSavePolicy.hpp"
#include "Engine/Runtime/Command/DeleteNodesCommand.hpp"
#include "Engine/Runtime/Command/DuplicateNodesCommand.hpp"

#include <spdlog/spdlog.h>
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/NextQuickJS/NextQuickJSModule.hpp"
#include "Application/Common/DemoScenes.hpp"
#include "Application/Editor/Common/MultiViewportBackend.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    Modules::NextQuickJS::Install(*engine, {.compileTypeScript = false, .enableHotReload = false});
    editorUserInterface_ = std::make_unique<EditorInterface>(this);

    NextRenderer::HideConsole();

    glm::ivec2 monitorSize = GetEngine().GetMonitorSize();

    // windows config
    config.Title = "gkNextEditor";
    uint32_t computedWidth = static_cast<uint32_t>(monitorSize.x * 0.75f);
    uint32_t computedHeight = static_cast<uint32_t>(monitorSize.y * 0.75f);
    config.Width = computedWidth < 1920u ? static_cast<uint32_t>(monitorSize.x) : computedWidth;
    config.Height = computedHeight < 1080u ? static_cast<uint32_t>(monitorSize.y) : computedHeight;
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
    cvars.SetDefaultFromString("r.samples", "4", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
    cvars.SetDefaultFromString("r.superResolution", "2", &error);
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
    cvars.RegisterUserFileChannel("ed.", "assets/configs/cvar_user.editor.json");
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
    actions_.RegisterAction(EEditorAction::IO_LoadHDRI,
                            [](EditorContext& ctx, std::string_view args) -> bool
                            {
                                const std::string filename(args);
                                const bool hasMountedEntry =
                                    Utilities::Package::FPackageFileSystem::GetInstance().HasMountedEntry(filename);
                                std::error_code existsError;
                                const bool hasFilesystemEntry = std::filesystem::exists(filename, existsError) ||
                                    std::filesystem::exists(Utilities::FileHelper::GetPlatformFilePath(filename.c_str()),
                                                            existsError);
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
                                    modelViewController_.Focus(center, radius);
                                }
                                return found;
                            });

    GetEngine().GetShowFlags().ShowEdge = true;

    // Open the scene passed on the command line (--load-scene) so the editor can start directly on a
    // scene (and supports automated --agent-validation screenshots). Without it the editor is empty.
    if (GOption != nullptr && !GOption->SceneName.empty())
    {
        GetEngine().RequestLoadScene({.filename = GOption->SceneName});
        GetEditorInterface().GetEditorUiState().currentScenePath = GOption->SceneName;
    }
}

void EditorGameInstance::OnTick(double deltaSeconds)
{
    const bool moving = modelViewController_.UpdateCamera(1.0f, deltaSeconds);
    GetEngine().SetProgressiveRendering(!moving, false);
}

void EditorGameInstance::OnSceneLoaded() { modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera()); }

void EditorGameInstance::OnPreConfigUI() { editorUserInterface_->Config(); }

bool EditorGameInstance::OnRenderUI()
{
    editorUserInterface_->Render();
    return true;
}

void EditorGameInstance::OnInitUI() { editorUserInterface_->Init(); }

bool EditorGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE camera movement (only active when right mouse is pressed)
    modelViewController_.OnKey(event);

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
                if (GetEngine().GetScene().Save(savePath))
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
                SPDLOG_INFO("Current scene is not writable as GLB; use File > Save Scene As...");
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
                    modelViewController_.Focus(focusCenter, radius);
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

    if (!gizmoController_.IsInteracting())
    {
        modelViewController_.OnCursorPosition(xpos, ypos);
    }

    const uint32_t mouseButtons = GetEngine().GetMouseButtons();
    const bool rightMousePressed = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    if (settings_.hoverHighlight && !gizmoController_.IsInteracting() && !rightMousePressed)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCastGPU(org, dir,
                               [this](Assets::RayCastResult result)
                               {
                                   if (result.Hitted)
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
    if (!gizmoController_.IsInteracting())
    {
        modelViewController_.OnMouseButton(event);
    }
    else
    {
        return true;
    }
    if (event.button.button == SDL_BUTTON_LEFT && event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        const bool toggleSelection = (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCastGPU(org, dir,
                               [this, toggleSelection](Assets::RayCastResult result)
                               {
                                   if (result.Hitted)
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
    return true;
}

bool EditorGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (!gizmoController_.IsInteracting())
    {
        modelViewController_.OnScroll(xoffset, yoffset);
    }
    return true;
}

void EditorGameInstance::DrawGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize)
{
    gizmoController_.Draw(GetEngine(), viewportPos, viewportSize, settings_.gizmoSnap,
                          settings_.gizmoSnapTranslate, settings_.gizmoDefaultMode);
}
