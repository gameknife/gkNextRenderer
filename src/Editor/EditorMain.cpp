#include "EditorMain.h"
#include <Runtime/Platform/PlatformCommon.h>
#include "Assets/Core/Node.h"
#include "Assets/GPU/Texture.hpp"
#include "EditorInterface.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"

#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorContext.hpp"

#include <spdlog/spdlog.h>

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine), engine_(engine)
{
    editorUserInterface_ = std::make_unique<EditorInterface>(this);

    NextRenderer::HideConsole();

    glm::ivec2 monitorSize = GetEngine().GetMonitorSize();

    // windows config
    config.Title = "NextEditor";
    config.Width = static_cast<uint32_t>(monitorSize.x * 0.75f);
    config.Height = static_cast<uint32_t>(monitorSize.y * 0.75f);
    config.ForceSDR = true;
    config.HideTitleBar = true;

    options.ForceSDR = true;
    options.KeepCPUMeshData = true; // 编辑器模式保留CPU网格数据用于场景保存
}

void EditorGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "8", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
    cvars.SetDefaultFromString("r.superResolution", "2", &error);
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
                            [](EditorContext& ctx, std::string_view args) -> bool
                            {
                                ctx.engine.GetCommandSystem().Clear();
                                ctx.engine.RequestLoadScene(std::string(args));
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::IO_LoadSceneAdd,
                            [](EditorContext& ctx, std::string_view args) -> bool
                            {
                                ctx.engine.GetCommandSystem().Clear();
                                ctx.engine.RequestLoadSceneAdd(std::string(args));
                                return true;
                            });
    actions_.RegisterAction(EEditorAction::IO_LoadHDRI,
                            [](EditorContext& ctx, std::string_view args) -> bool
                            {
                                const std::string filename(args);
                                std::error_code existsError;
                                if (!std::filesystem::exists(filename, existsError))
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
    // GetEngine().GetUserSettings().SceneEpsilonScale = 0.01f;
    // GetEngine().GetUserSettings().AmbientCubeUnit = 0.02f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetX = 0.0f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetZ = 0.0f;
}

void EditorGameInstance::OnTick(double deltaSeconds)
{
    modelViewController_.UpdateCamera(1.0f, deltaSeconds);
    //GetEngine().SetProgressiveRendering(!moving, false);
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

    const uint32_t mouseButtons = SDL_GetMouseState(nullptr, nullptr);
    const bool rightMousePressed = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    if (!gizmoController_.IsInteracting() && !rightMousePressed)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        NextEngineHelper::GetScreenToWorldRay(mousePos, org, dir);
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
        NextEngineHelper::GetScreenToWorldRay(mousePos, org, dir);
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
                                       NextEngineHelper::DrawAuxPoint(result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2,
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
    gizmoController_.Draw(*engine_, viewportPos, viewportSize);
}
