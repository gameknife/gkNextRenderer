#include "EditorMain.h"
#include <imgui_internal.h>
#include <Runtime/Platform/PlatformCommon.h>

#include "EditorInterface.hpp"
#include "Runtime/Engine.hpp"

#include "Editor/EditorCommand.hpp"
#include "Editor/EditorInterface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine): NextGameInstanceBase(config, options, engine), engine_(engine)
{
    editorUserInterface_ = std::make_unique<EditorInterface>(this);

    NextRenderer::HideConsole();

    glm::ivec2 MonitorSize = GetEngine().GetMonitorSize();

    // windows config
    config.Title = "NextEditor";
    config.Width = static_cast<uint32_t>(MonitorSize.x * 0.75f);
    config.Height = static_cast<uint32_t>(MonitorSize.y * 0.75f);
    config.ForceSDR = true;
    config.HideTitleBar = true;

    options.Samples = 8;
    options.Temporal = 16;
    options.ForceSDR = true;
    options.NoDenoiser = true;
    options.SuperResolution = 0;
}

void EditorGameInstance::OnInit()
{
    // EditorCommand, need Refactoring
    EditorCommand::RegisterEdtiorCommand(EEditorCommand::ECmdSystem_RequestExit, [this](std::string& args)-> bool
    {
        GetEngine().GetWindow().Close();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand(EEditorCommand::ECmdSystem_RequestMaximum, [this](std::string& args)-> bool
    {
        GetEngine().GetWindow().Maximum();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand(EEditorCommand::ECmdSystem_RequestMinimize, [this](std::string& args)-> bool
    {
        GetEngine().GetWindow().Minimize();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand(EEditorCommand::ECmdIO_LoadScene, [this](std::string& args)-> bool
    {
        GetEngine().RequestLoadScene(args);
        return true;
    });
    EditorCommand::RegisterEdtiorCommand(EEditorCommand::ECmdIO_LoadHDRI, [this](std::string& args)-> bool
    {
        //Assets::GlobalTexturePool::UpdateHDRTexture(0, args.c_str(), Vulkan::SamplerConfig());
        //GetEngine().GetUserSettings().SkyIdx = 0;
        return true;
    });

    GetEngine().GetUserSettings().ShowEdge = true;
}

void EditorGameInstance::OnTick(double deltaSeconds)
{
    bool moving = modelViewController_.UpdateCamera(1.0f, deltaSeconds);
    GetEngine().SetProgressiveRendering(!moving, false);
}

void EditorGameInstance::OnSceneLoaded()
{
    modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
}

void EditorGameInstance::OnPreConfigUI()
{
    editorUserInterface_->Config();
}

bool EditorGameInstance::OnRenderUI()
{
    editorUserInterface_->Render();
    return true;
}

void EditorGameInstance::OnInitUI()
{
    editorUserInterface_->Init();
}

bool EditorGameInstance::OnKey(int key, int scancode, int action, int mods)
{
    modelViewController_.OnKey(key, scancode, action, mods);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        GetEngine().GetScene().SetSelectedId(-1);
    }
    return true;
}

bool EditorGameInstance::OnCursorPosition(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(xpos, ypos);
    return true;
}

bool EditorGameInstance::OnMouseButton(int button, int action, int mods)
{
    modelViewController_.OnMouseButton(button, action, mods);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        GetEngine().GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCastGPU(org, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hitted)
            {
                GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                GetEngine().DrawAuxPoint(result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 30);
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
    modelViewController_.OnScroll(xoffset, yoffset);
    return true;
}
