#include "EditorMain.h"
#include <imgui_internal.h>
#include <Runtime/Platform/PlatformCommon.h>

#include "EditorInterface.hpp"
#include "Runtime/Application.hpp"

#include "Editor/EditorCommand.hpp"
#include "Editor/EditorInterface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine): NextGameInstanceBase(config, options, engine), engine_(engine)
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

    options.Samples = 4;
    options.ForceSDR = true;
}

void EditorGameInstance::OnInit()
{
    // EditorCommand, need Refactoring
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestExit, [this](std::string& args)->bool {
        GetEngine().GetWindow().Close();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestMaximum, [this](std::string& args)->bool {
        GetEngine().GetWindow().Maximum();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdSystem_RequestMinimize, [this](std::string& args)->bool {
        GetEngine().GetWindow().Minimize();
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdIO_LoadScene, [this](std::string& args)->bool {
        GetEngine().RequestLoadScene(args);
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdIO_LoadHDRI, [this](std::string& args)->bool {
        //Assets::GlobalTexturePool::UpdateHDRTexture(0, args.c_str(), Vulkan::SamplerConfig());
        //GetEngine().GetUserSettings().SkyIdx = 0;
        return true;
    });

    GetEngine().GetUserSettings().ShowEdge = true;
}

void EditorGameInstance::OnTick(double deltaSeconds)
{
    
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
    return false;
}

bool EditorGameInstance::OnCursorPosition(double xpos, double ypos)
{
    return false;
}

bool EditorGameInstance::OnMouseButton(int button, int action, int mods)
{
    return false;
}
