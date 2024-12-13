#include "EditorInterface.hpp"

#include "Runtime/SceneList.hpp"
#include "Runtime/UserSettings.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Instance.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/Surface.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <filesystem>
#include <Editor/EditorGUI.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

#include "Options.hpp"
#include "Assets/Scene.hpp"
#include "Assets/TextureImage.hpp"
#include "Editor/EditorMain.h"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Editor/IconsFontAwesome6.h"

extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

namespace
{
	void CheckVulkanResultCallback(const VkResult err)
	{
		if (err != VK_SUCCESS)
		{
			Throw(std::runtime_error(std::string("ImGui Vulkan error (") + Vulkan::ToString(err) + ")"));
		}
	}

	const ImWchar*  GetGlyphRangesFontAwesome()
	{
		static const ImWchar ranges[] =
		{
			ICON_MIN_FA, ICON_MAX_FA, // Basic Latin + Latin Supplement
			0,
		};
		return &ranges[0];
	}
}

EditorInterface::EditorInterface(class EditorGameInstance* editor)
	: editor_(editor)
{
	
}

EditorInterface::~EditorInterface()
{
	editorGUI_.reset();
}

void EditorInterface::Config()
{
	auto& io = ImGui::GetIO();

	io.IniFilename = "editor.ini";
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void EditorInterface::Init()
{
	editorGUI_.reset(new Editor::GUI());
	auto& io = ImGui::GetIO();
		
	// Window scaling and style.
	const auto scaleFactor = 1.0;
	//ImGui::GetStyle().ScaleAllSizes(scaleFactor);
	
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
	const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic() : GOption->locale == "zhCN" ? io.Fonts->GetGlyphRangesChineseFull() : io.Fonts->GetGlyphRangesDefault();
	
	const ImWchar* iconRange = GetGlyphRangesFontAwesome();
	ImFontConfig config;
	config.MergeMode = true;
	config.GlyphMinAdvanceX = 14.0f;
	config.GlyphOffset = ImVec2(0, 0);
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 14 * scaleFactor, &config, iconRange ))
	{
		
	}

	fontIcon_ = io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf").c_str(), 18 * scaleFactor, nullptr, glyphRange );
	
	config.GlyphMinAdvanceX = 20.0f;
	config.GlyphOffset = ImVec2(0, 0);
	io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 18 * scaleFactor, &config, iconRange );

	fontBigIcon_ = io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 32 * scaleFactor, nullptr, iconRange );
	
	firstRun = true;

	editorGUI_->fontIcon_ = fontIcon_;
	editorGUI_->bigIcon_ = fontBigIcon_;
}

const float toolbarSize = 50;
const float toolbarIconWidth = 32;
const float toolbarIconHeight = 32;
const float titleBarHeight = 55;
const float footBarHeight = 40;
float menuBarHeight = 0;

ImGuiID EditorInterface::DockSpaceUI()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + toolbarSize + titleBarHeight - menuBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - toolbarSize - titleBarHeight + menuBarHeight - footBarHeight));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowBgAlpha(0);
	ImGuiWindowFlags window_flags = 0
		| ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("Master DockSpace", NULL, window_flags);
	ImGuiID dockMain = ImGui::GetID("MyDockspace");
	
	// Save off menu bar height for later.
	menuBarHeight = ImGui::GetCurrentWindow()->MenuBarHeight;

	ImGui::DockSpace(dockMain, ImVec2(0,0), ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
	ImGui::PopStyleVar(3);

	return dockMain;
}

void EditorInterface::ToolbarUI()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titleBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, toolbarSize));
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags window_flags = 0
		| ImGuiWindowFlags_NoDocking 
		| ImGuiWindowFlags_NoTitleBar 
		| ImGuiWindowFlags_NoResize 
		| ImGuiWindowFlags_NoMove 
		| ImGuiWindowFlags_NoScrollbar 
		| ImGuiWindowFlags_NoSavedSettings
		;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::Begin("TOOLBAR", NULL, window_flags);
	ImGui::PopStyleVar();
	ImGui::PopStyleVar();
	
	ImGui::BeginGroup();
	ImGui::PushFont(fontIcon_);
	ImGui::Button(ICON_FA_FLOPPY_DISK, ImVec2(toolbarIconWidth, toolbarIconHeight));ImGui::SameLine();
	ImGui::Button(ICON_FA_FOLDER, ImVec2(toolbarIconWidth, toolbarIconHeight));ImGui::SameLine();
	ImGui::PopFont();
	ImGui::EndGroup();ImGui::SameLine();

	ImGui::BeginGroup();ImGui::SameLine(50);
	ImGui::PushFont(fontIcon_);
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80,210,0,255));
	if( ImGui::Button(ICON_FA_PLAY, ImVec2(toolbarIconWidth, toolbarIconHeight)) )
	{
		std::filesystem::path currentPath = std::filesystem::current_path();
		std::string cmdline = (currentPath / "gkNextRenderer").string() + (GOption->ForceSDR ? " --forcesdr" : "");
		std::system(cmdline.c_str());
	}
	ImGui::SameLine();
	
	ImGui::PopStyleColor();
	ImGui::PopFont();
	static int item = 3;
	static float color[4] = { 0.4f, 0.7f, 0.0f, 0.5f };
	ImGui::SetNextItemWidth(120);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7,7));
	ImGui::Combo("##Render", &item, "RTPipe\0ModernDeferred\0LegacyDeferred\0RayQuery\0HybirdRender\0\0");ImGui::SameLine();
	ImGui::PopStyleVar();
	ImGui::EndGroup();ImGui::SameLine();


	ImGui::BeginGroup();ImGui::SameLine(50);
	ImGui::PushFont(fontIcon_);
	ImGui::Button(ICON_FA_FILE_IMPORT, ImVec2(toolbarIconWidth, toolbarIconHeight));ImGui::SameLine();
	ImGui::PopFont();
	ImGui::EndGroup();
	
	ImGui::End();
}

void EditorInterface::Render()
{
	GUserInterface = editor_->GetEngine().GetUserInterface();
		
	editorGUI_->selected_obj_id = editor_->GetEngine().GetScene().GetSelectedId();
	
	ImGuiID id = DockSpaceUI();
	ToolbarUI();
	
	MainWindowGUI(*editorGUI_, editor_->GetEngine().GetScene(), id, firstRun);
	
	ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	editor_->GetEngine().GetRenderer().SwapChain().UpdateEditorViewport(Utilities::Math::floorToInt(node->Pos.x - viewport->Pos.x), Utilities::Math::floorToInt(node->Pos.y - viewport->Pos.y), Utilities::Math::ceilToInt(node->Size.x), Utilities::Math::ceilToInt(node->Size.y));

	firstRun = false;
	GUserInterface = nullptr;
}

void EditorInterface::DrawIndicator(uint32_t frameCount)
{
	ImGui::OpenPopup("Loading");
	// Always center this window when appearing
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(100,40));

	if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
	{
		ImGui::Text("Loading%s   ", frameCount % 4 == 0 ? "" : frameCount % 4 == 1 ? "." : frameCount % 4 == 2 ? ".." : "...");
		ImGui::EndPopup();
	}
}


void EditorInterface::MainWindowGUI(Editor::GUI & gui_r, Assets::Scene& scene, ImGuiID id, bool firstRun)
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
