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

EditorInterface::EditorInterface(
	Vulkan::CommandPool& commandPool, 
	const Vulkan::SwapChain& swapChain, 
	const Vulkan::DepthBuffer& depthBuffer)
{
	editorGUI_.reset(new Editor::GUI());
	
	const auto& device = swapChain.Device();
	const auto& window = device.Surface().Instance().Window();

	// Initialise descriptor pool and render pass for ImGui.
	const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
	};
	descriptorPool_.reset(new Vulkan::DescriptorPool(device, descriptorBindings, swapChain.MinImageCount() + 256));
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	
	// Initialise ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	
	auto& io = ImGui::GetIO();
	// No ini file.
	io.IniFilename = "imgui.ini";

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


	// Initialise ImGui GLFW adapter
	if (!ImGui_ImplGlfw_InitForVulkan(window.Handle(), true))
	{
		Throw(std::runtime_error("failed to initialise ImGui GLFW adapter"));
	}
	
	// Initialise ImGui Vulkan adapter
	ImGui_ImplVulkan_InitInfo vulkanInit = {};
	vulkanInit.Instance = device.Surface().Instance().Handle();
	vulkanInit.PhysicalDevice = device.PhysicalDevice();
	vulkanInit.Device = device.Handle();
	vulkanInit.QueueFamily = device.GraphicsFamilyIndex();
	vulkanInit.Queue = device.GraphicsQueue();
	vulkanInit.PipelineCache = nullptr;
	vulkanInit.DescriptorPool = descriptorPool_->Handle();
	vulkanInit.MinImageCount = swapChain.MinImageCount();
	vulkanInit.ImageCount = static_cast<uint32_t>(swapChain.Images().size());
	vulkanInit.Allocator = nullptr;
	vulkanInit.CheckVkResultFn = CheckVulkanResultCallback;
	vulkanInit.RenderPass = renderPass_->Handle();

	if (!ImGui_ImplVulkan_Init(&vulkanInit))
	{
		Throw(std::runtime_error("failed to initialise ImGui vulkan adapter"));
	}
	
	// Window scaling and style.
#if __APPLE__
	const auto scaleFactor = 1.0;
#else
    const auto scaleFactor = window.ContentScale();
#endif

	UserInterface::SetStyle();
	ImGui::GetStyle().ScaleAllSizes(scaleFactor);


	// Upload ImGui fonts (use ImGuiFreeType for better font rendering, see https://github.com/ocornut/imgui/tree/master/misc/freetype).
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
	const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic() : GOption->locale == "zhCN" ? io.Fonts->GetGlyphRangesChineseFull() : io.Fonts->GetGlyphRangesDefault();
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-Regular.ttf").c_str(), 14 * scaleFactor, nullptr, glyphRange ))
	{
		Throw(std::runtime_error("failed to load basic ImGui Text font"));
	}
	
	ImFontConfig configLocale;
	configLocale.MergeMode = true;
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf").c_str(), 14 * scaleFactor, &configLocale, glyphRange ))
	{
		Throw(std::runtime_error("failed to load locale ImGui Text font"));
	}
	
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
	
	Vulkan::SingleTimeCommands::Submit(commandPool, [] (VkCommandBuffer commandBuffer)
	{
		if (!ImGui_ImplVulkan_CreateFontsTexture())
		{
			Throw(std::runtime_error("failed to create ImGui font textures"));
		}
	});
	
	firstRun = true;

	editorGUI_->fontIcon_ = fontIcon_;
	editorGUI_->bigIcon_ = fontBigIcon_;
}

EditorInterface::~EditorInterface()
{
	uiFrameBuffers_.clear();
	editorGUI_.reset();
	
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

const float toolbarSize = 50;
const float toolbarIconWidth = 32;
const float toolbarIconHeight = 32;
const float titleBarHeight = 55;
const float footBarHeight = 40;
float menuBarHeight = 0;


VkDescriptorSet EditorInterface::RequestImTextureId(uint32_t globalTextureId)
{
	if( imTextureIdMap_.find(globalTextureId) == imTextureIdMap_.end() )
	{
		auto texture = Assets::GlobalTexturePool::GetTextureImage(globalTextureId);
		if(texture)
		{
			imTextureIdMap_[globalTextureId] = ImGui_ImplVulkan_AddTexture(texture->Sampler().Handle(), texture->ImageView().Handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			return imTextureIdMap_[globalTextureId];
		}
	}
	else
	{
		return imTextureIdMap_[globalTextureId];
	}
	return VK_NULL_HANDLE;
}

void EditorInterface::OnCreateSurface(const Vulkan::SwapChain& swapChain, const Vulkan::DepthBuffer& depthBuffer)
{
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	
	for (const auto& imageView : swapChain.ImageViews())
	{
		uiFrameBuffers_.emplace_back(*imageView, *renderPass_, false);
	}
}

void EditorInterface::OnDestroySurface()
{
	renderPass_.reset();
	uiFrameBuffers_.clear();
}

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

void EditorInterface::PreRender()
{
}

void EditorInterface::Render(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, Assets::Scene* scene)
{
	GUserInterface = this;
	
	uint32_t count = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
	for ( uint32_t i = 0; i < count; ++i )
	{
		RequestImTextureId(i);
	}
	
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	
	editorGUI_->selected_obj_id = scene->GetSelectedId();
	
	ImGuiID id = DockSpaceUI();
	ToolbarUI();
	
	MainWindowGUI(*editorGUI_, scene, statistics, id, firstRun);
	
	if( statistics.LoadingStatus ) DrawIndicator(static_cast<uint32_t>(std::floor(statistics.RenderTime * 2)));
}

void EditorInterface::PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx)
{
	ImGuiID id = DockSpaceUI();
	ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	swapChain.UpdateEditorViewport(Utilities::Math::floorToInt(node->Pos.x - viewport->Pos.x), Utilities::Math::floorToInt(node->Pos.y - viewport->Pos.y), Utilities::Math::ceilToInt(node->Size.x), Utilities::Math::ceilToInt(node->Size.y));
	
	auto& io = ImGui::GetIO();
	
	ImGui::Render();

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass_->Handle();
	renderPassInfo.framebuffer = uiFrameBuffers_[imageIdx].Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = renderPass_->SwapChain().Extent();
	renderPassInfo.clearValueCount = 0;
	renderPassInfo.pClearValues = nullptr;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
	vkCmdEndRenderPass(commandBuffer);

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

	firstRun = false;

	GUserInterface = nullptr;
}

bool EditorInterface::WantsToCaptureKeyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorInterface::WantsToCaptureMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
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
