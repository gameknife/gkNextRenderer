#include "EditorMain.h"
#include <Runtime/Platform/PlatformCommon.h>
#include "Assets/Node.h"
#include "EditorInterface.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/NextEngineHelper.h"

#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorContext.hpp"

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

    options.Samples = 8;
    options.Temporal = 16;
    options.ForceSDR = true;
    options.NoDenoiser = true;
    options.SuperResolution = 2;
    options.KeepCPUMeshData = true; // 编辑器模式保留CPU网格数据用于场景保存
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
                            [](EditorContext& /*ctx*/, std::string_view /*args*/) -> bool
                            {
                                // TODO: integrate HDRI changes with scene/env settings.
                                return true;
                            });

    GetEngine().GetShowFlags().ShowEdge = true;
}

void EditorGameInstance::OnTick(double deltaSeconds)
{
    // bool moving = modelViewController_.UpdateCamera(1.0f, deltaSeconds);
    // GetEngine().SetProgressiveRendering(!moving, false);
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
    if (!gizmoController_.IsShowing())
    {
        modelViewController_.OnKey(event);
    }
    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_ESCAPE:
            GetEngine().GetScene().SetSelectedId(-1);
            break;
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
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        NextEngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCastGPU(org, dir,
                               [this](Assets::RayCastResult result)
                               {
                                   if (result.Hitted)
                                   {
                                       GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                                       NextEngineHelper::DrawAuxPoint(result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2,
                                                                      30);
                                       // selection
                                       GetEngine().GetScene().SetSelectedId(result.InstanceId);
                                   }
                                   else
                                   {
                                       GetEngine().GetScene().SetSelectedId(-1);
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
