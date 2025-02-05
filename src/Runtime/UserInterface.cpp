#include "UserInterface.hpp"

#include "SceneList.hpp"
#include "UserSettings.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Instance.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/Surface.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Engine.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#if !ANDROID
#include <imgui_impl_glfw.h>
#else
#include <imgui_impl_android.h>
#endif
#include <imgui_impl_vulkan.h>

#include <array>
#include <filesystem>
#include <Editor/EditorGUI.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

#include "Engine.hpp"
#include "Options.hpp"
#include "Assets/TextureImage.hpp"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Editor/IconsFontAwesome6.h"
#include "Utilities/ImGui.hpp"
#include "Vulkan/ImageView.hpp"

extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;


UserInterface::UserInterface(
	NextEngine* engine,
	Vulkan::CommandPool& commandPool, 
	const Vulkan::SwapChain& swapChain, 
	const Vulkan::DepthBuffer& depthBuffer,
	UserSettings& userSettings, std::function<void()> funcPreConfig, std::function<void()> funcInit) :
	userSettings_(userSettings),
	engine_(engine)
{
	const auto& device = swapChain.Device();
	const auto& window = device.Surface().Instance().Window();

	// Initialise descriptor pool and render pass for ImGui.
	const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
	};
	descriptorPool_.reset(new Vulkan::DescriptorPool(device, descriptorBindings, swapChain.MinImageCount() + 2048));
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	
	// Initialise ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	
	auto& io = ImGui::GetIO();
	// No ini file.
	io.IniFilename = "imgui.ini";
	io.WantCaptureMouse = false;
	io.WantCaptureKeyboard = false;

	funcPreConfig();
	
	// Initialise ImGui GLFW adapter
#if !ANDROID
	if (!ImGui_ImplGlfw_InitForVulkan(window.Handle(), true))
	{
		Throw(std::runtime_error("failed to initialise ImGui GLFW adapter"));
	}
#else
	ImGui_ImplAndroid_Init(window.Handle());
#endif

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
	vulkanInit.RenderPass = renderPass_->Handle();

	if (!ImGui_ImplVulkan_Init(&vulkanInit))
	{
		Throw(std::runtime_error("failed to initialise ImGui vulkan adapter"));
	}
	
	// Window scaling and style.
#if ANDROID
    const auto scaleFactor = 1.0;
#elif __APPLE__
	const auto scaleFactor = 1.0;
#else
    const auto scaleFactor = 1.0;//window.ContentScale();
#endif

	const auto fontSize = 16;

	UserInterface::SetStyle();
	ImGui::GetStyle().ScaleAllSizes(scaleFactor);
	
	// Upload ImGui fonts (use ImGuiFreeType for better font rendering, see https://github.com/ocornut/imgui/tree/master/misc/freetype).
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
	const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic() : GOption->locale == "zhCN" ? io.Fonts->GetGlyphRangesChineseFull() : io.Fonts->GetGlyphRangesDefault();
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-Regular.ttf").c_str(), fontSize * scaleFactor, nullptr, glyphRange ))
	{
		Throw(std::runtime_error("failed to load basic ImGui Text font"));
	}

	static const ImWchar iconRange[] =
		{
		ICON_MIN_FA, ICON_MAX_FA, // Basic Latin + Latin Supplement
		0,
	};
	ImFontConfig config;
	config.MergeMode = true;
	config.GlyphMinAdvanceX = fontSize;
	config.GlyphOffset = ImVec2(0, 0);
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-regular-400.ttf").c_str(), fontSize * scaleFactor, &config, iconRange ))
	{
		
	}
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), fontSize * scaleFactor, &config, iconRange ))
	{
		
	}
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-brands-400.ttf").c_str(), fontSize * scaleFactor, &config, iconRange ))
	{
		
	}
#if !ANDROID
	ImFontConfig configLocale;
	configLocale.MergeMode = true;
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf").c_str(), (fontSize + 2) * scaleFactor, &configLocale, glyphRange ))
	{
		Throw(std::runtime_error("failed to load locale ImGui Text font"));
	}
#endif

	if(funcInit != nullptr)
	{
		funcInit();
	}

	Vulkan::SingleTimeCommands::Submit(commandPool, [] (VkCommandBuffer commandBuffer)
	{
		if (!ImGui_ImplVulkan_CreateFontsTexture())
		{
			Throw(std::runtime_error("failed to create ImGui font textures"));
		}
	});
}

UserInterface::~UserInterface()
{
	uiFrameBuffers_.clear();
	
	ImGui_ImplVulkan_Shutdown();
#if !ANDROID
	ImGui_ImplGlfw_Shutdown();
#else
	ImGui_ImplAndroid_Shutdown();
#endif
	ImGui::DestroyContext();
}

void UserInterface::OnCreateSurface(const Vulkan::SwapChain& swapChain, const Vulkan::DepthBuffer& depthBuffer)
{
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	
	for (const auto& imageView : swapChain.ImageViews())
	{
		uiFrameBuffers_.emplace_back(*imageView, *renderPass_, false);
	}
}

void UserInterface::OnDestroySurface()
{
	renderPass_.reset();
	uiFrameBuffers_.clear();
}

VkDescriptorSet UserInterface::RequestImTextureId(uint32_t globalTextureId)
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

VkDescriptorSet UserInterface::RequestImTextureByName(const std::string& name)
{
	uint32_t id = Assets::GlobalTexturePool::GetTextureIndexByName(name);
	if( id == -1)
	{
		return VK_NULL_HANDLE;
	}
	return RequestImTextureId(id);
}

void UserInterface::SetStyle()
{
    ImGuiIO &io = ImGui::GetIO();

    io.IniFilename              = NULL;

	ImVec4 ActiveColor = true ? ImVec4(0.42f, 0.45f, 0.5f, 1.00f) : ImVec4(0.28f, 0.45f, 0.70f, 1.00f);

    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    ImGui::StyleColorsDark(style);
    colors[ImGuiCol_Text]                   = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.10f, 0.10f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.0f, 0.0f, 0.0f, 1.00f); // TrueBlack
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ActiveColor;
    colors[ImGuiCol_SliderGrab]             = ActiveColor;
    colors[ImGuiCol_SliderGrabActive]       = ActiveColor;
    colors[ImGuiCol_Button]                 = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ActiveColor;
    colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ActiveColor;
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ActiveColor;
    colors[ImGuiCol_SeparatorActive]        = ActiveColor;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.54f, 0.54f, 0.54f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ActiveColor;
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.19f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ActiveColor;
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.20f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ActiveColor;
    colors[ImGuiCol_NavHighlight]           = ActiveColor;
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	
    style->WindowPadding                    = ImVec2(12.00f, 8.00f);
    style->ItemSpacing                      = ImVec2(6.00f, 6.00f);
    style->GrabMinSize                      = 20.00f;
    style->WindowRounding                   = 8.00f;
    style->FrameBorderSize                  = 0.00f;
    style->FrameRounding                    = 4.00f;
    style->GrabRounding                     = 12.00f;
	style->SeparatorTextBorderSize			= 1.0f;
}

void UserInterface::DrawPoint(float x, float y, float size, glm::vec4 color)
{
	// in viewport mode, the start from the display
	auxDrawRequest_.push_back( [=]() {
		ImVec2 StartPos = ImGui::GetMainViewport()->Pos;
		ImGui::GetForegroundDrawList()->AddRectFilled(StartPos + ImVec2{x - size, y - size}, StartPos + ImVec2{x + size, y + size}, Utilities::UI::Vec4ToImU32(color));
	});
}

void UserInterface::DrawLine(float fromx, float fromy, float tox, float toy, float size, glm::vec4 color)
{
	auxDrawRequest_.push_back( [=]() {
		ImVec2 StartPos = ImGui::GetMainViewport()->Pos;
		ImGui::GetForegroundDrawList()->AddLine( StartPos + ImVec2(fromx, fromy), StartPos + ImVec2(tox, toy), Utilities::UI::Vec4ToImU32(color), size);
	});
}

void UserInterface::PreRender()
{
	ImGui_ImplVulkan_NewFrame();
#if !ANDROID
	ImGui_ImplGlfw_NewFrame();
#else
	ImGui_ImplAndroid_NewFrame();
#endif
	ImGui::NewFrame();


	// update texture to ui
	uint32_t maxId = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
	for ( uint32_t idx = 0; idx < maxId; ++idx)
	{
		RequestImTextureId(idx);
	}
}

void UserInterface::Render(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, Assets::Scene* scene)
{
	DrawOverlay(statistics, gpuTimer);
}

void UserInterface::PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx)
{
	if( GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Loading ) DrawIndicator(GetEngine().GetTotalFrames());
	
	// aux
	for( auto& req : auxDrawRequest_) {
		req();
	}
	auxDrawRequest_.clear();
	
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

	auto& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

bool UserInterface::WantsToCaptureKeyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool UserInterface::WantsToCaptureMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
}

void UserInterface::DrawOverlay(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer)
{
	if (!Settings().ShowOverlay)
	{
		return;
	}

	const auto& io = ImGui::GetIO();
	const float distance = 10.0f;
#if ANDROID
	const ImVec2 pos = ImVec2(io.DisplaySize.x * 0.5 - distance, distance);
	const ImVec2 posPivot = ImVec2(1.0f, 0.0f);
#else
	const ImVec2 pos = ImVec2(io.DisplaySize.x - distance, distance);
	const ImVec2 posPivot = ImVec2(1.0f, 0.0f);
#endif
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
	ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background

	const auto flags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Statistics", &Settings().ShowOverlay, flags))
	{
		ImGui::Text("Statistics (%dx%d):", statistics.FramebufferSize.width, statistics.FramebufferSize.height);
		ImGui::Text("%s", statistics.Stats["gpu"].c_str());
		ImGui::Separator();
		ImGui::Text("Frame rate: %.0f fps", statistics.FrameRate);
		//ImGui::Text("Campos:  %.2f %.2f %.2f", statistics.CamPosX, statistics.CamPosY, statistics.CamPosZ);
		
		ImGui::Text("Tris: %s", Utilities::metricFormatter(static_cast<double>(statistics.TriCount), "").c_str());

		
		ImGui::Text("Instance: %s", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "").c_str());
		ImGui::Text("Node: %s", Utilities::metricFormatter(static_cast<double>(statistics.NodeCount), "").c_str());
		
		ImGui::Text("Texture: %d", statistics.TextureCount);

		ImGui::Text("frametime: %.2fms", statistics.FrameTime);
		// auto fetch timer & display
		auto times = gpuTimer->FetchAllTimes();
		for(auto& time : times)
		{
			ImGui::Text("%s: %.2fms", std::get<0>(time).c_str(), std::get<1>(time));
		}

		ImGui::Text("drawframe: %.2fms", gpuTimer->GetCpuTime("draw-frame"));
		ImGui::Text(" query: %.2fms", gpuTimer->GetCpuTime("query-wait"));
		ImGui::Text(" render: %.2fms", gpuTimer->GetCpuTime("render"));
		ImGui::Text(" uniform: %.2fms", gpuTimer->GetCpuTime("cpugpu-io"));
		ImGui::Text(" fence: %.2fms", gpuTimer->GetCpuTime("sync-wait"));
		ImGui::Text(" submit: %.2fms", gpuTimer->GetCpuTime("submit"));
		ImGui::Text(" present: %.2fms", gpuTimer->GetCpuTime("present"));
		
		ImGui::Text("Frame: %d", statistics.TotalFrames);
		
		ImGui::Text("Time: %s", fmt::format("{:%H:%M:%S}", std::chrono::seconds(static_cast<long long>(statistics.RenderTime))).c_str());
	}
	ImGui::End();

	// if( Settings().AutoFocus )
	// {
	// 	// draw a center dot with imgui
	// 	auto io = ImGui::GetIO();
	// 	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	// 	ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
	// 	ImGui::SetNextWindowSize(ImVec2(8, 8));
	//
	// 	// set border color
	// 	ImGui::PushStyleColor(ImGuiCol_Border, !Settings().AutoFocus ? ImVec4(1,1,1,1) : Settings().HitResult.Hitted ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f): ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
	// 	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1,1));
	// 	ImGui::Begin("CenterDot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	// 	//ImGui::Text(" ");
	// 	ImGui::End();
	// 	ImGui::PopStyleColor();
	// 	ImGui::PopStyleVar();
	//
	// 	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	// 	ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
	// 	ImGui::SetNextWindowSize(ImVec2(1, 1));
	//
	// 	// set border color
	// 	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f + 10.0f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0, 0.5f));
	// 	ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
	// 	ImGui::SetNextWindowSize(ImVec2(0, 0));
	// 	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
	// 	ImGui::Begin("HitInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	// 	ImGui::Text("%.1fm\nInst: %d\nMat: %d", Settings().HitResult.T, Settings().HitResult.InstanceId, Settings().HitResult.MaterialId);
	// 	ImGui::End();
	// 	ImGui::PopStyleColor();
	// 	
	// }
}

void UserInterface::DrawIndicator(uint32_t frameCount)
{
	frameCount /= 60;
	ImGui::OpenPopup("Loading");
	// Always center this window when appearing
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(200,40));

	if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
	{
		ImGui::Text("Loading%s", frameCount % 4 == 0 ? "" : frameCount % 4 == 1 ? "." : frameCount % 4 == 2 ? ".." : "...");
		ImGui::EndPopup();
	}
}
