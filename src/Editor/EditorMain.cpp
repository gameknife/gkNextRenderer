#include "EditorMain.h"
#include <imgui_internal.h>
#include <Runtime/Platform/PlatformWindows.h>

#include "EditorInterface.hpp"
#include "Runtime/Application.hpp"

#include "Editor/EditorCommand.hpp"
#include "Editor/EditorInterface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<EditorGameInstance>(config, options, engine);
}

void MainWindowGUI(Editor::GUI & gui_r, Assets::Scene& scene, ImGuiID id, bool firstRun)
{
    //////////////////////////////////
    Editor::GUI &gui = gui_r;

    gui.current_scene = &scene;
    
    // Only run DockBuilder functions on the first frame of the app:
    if (firstRun) {
        ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.1f, nullptr, &id);
        ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.2f, nullptr, &id);
        ImGuiID dock3 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.25f, nullptr, &id);

        ImGui::DockBuilderDockWindow("Outliner", dock1);
        ImGui::DockBuilderDockWindow("Properties", dock2);
        ImGui::DockBuilderDockWindow("Content Browser", dock3);

        ImGui::DockBuilderFinish(id);
    }

    {
        gui.ShowMenubar();

        // create-sidebar
        if (gui.sidebar) gui.ShowSidebar(&scene);

        // create-properties
        if (gui.properties) gui.ShowProperties();

        // workspace-output
        if (gui.contentBrowser) gui.ShowContentBrowser();

        // create-viewport
        if (gui.viewport) gui.ShowViewport(id);
    }

    { // create-children
        if (gui.child_style) utils::ShowStyleEditorWindow(&gui.child_style);
        if (gui.child_demo) ImGui::ShowDemoWindow(&gui.child_demo);
        if (gui.child_metrics) ImGui::ShowMetricsWindow(&gui.child_metrics);
        if (gui.child_stack) ImGui::ShowStackToolWindow(&gui.child_stack);
        if (gui.child_color) utils::ShowColorExportWindow(&gui.child_color);
        if (gui.child_resources) utils::ShowResourcesWindow(&gui.child_resources);
        if (gui.child_about) utils::ShowAboutWindow(&gui.child_about);
        //if (gui.child_mat_editor) utils::ShowDemoMaterialEditor(&gui.child_mat_editor);
    }

    {
        if(gui.ed_material) gui.ShowMaterialEditor();
    }

}

EditorGameInstance::EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine): NextGameInstanceBase(config, options, engine), engine_(engine)
{
    editorUserInterface_ = std::make_unique<EditorInterface>(this);

    NextRenderer::HideConsole();

    // windows config
    config.Title = "NextEditor";
    config.Height = 1080;
    config.Width = 1920;
    config.ForceSDR = true;
    config.HideTitleBar = true;
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
        //userSettings_.SceneIndex = SceneList::AddExternalScene(args);
        //RequestLoadScene(args);
        return true;
    });
    EditorCommand::RegisterEdtiorCommand( EEditorCommand::ECmdIO_LoadHDRI, [this](std::string& args)->bool {
        //Assets::GlobalTexturePool::UpdateHDRTexture(0, args.c_str(), Vulkan::SamplerConfig());
        //userSettings_.SkyIdx = 0;
        return true;
    });
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
