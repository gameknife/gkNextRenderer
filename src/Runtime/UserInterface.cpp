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
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#else
#include <ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h>
#include <ThirdParty/imgui-custom/imgui_impl_vulkan_custom.h>
#endif



#include <array>
#include <filesystem>
#include <Editor/EditorGUI.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Options.hpp"
#include "TaskCoordinator.hpp"
#include "Assets/TextureImage.hpp"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Math.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Utilities/ImGui.hpp"
#include "Vulkan/ImageView.hpp"

extern float GAndroidMagicScale;
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
	renderPass_->SetDebugName("ImGui Render Pass");
	
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
	if (!ImGui_ImplSDL3_InitForVulkan(window.Handle()))
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
	vulkanInit.RenderPass = renderPass_->Handle();

	if (!ImGui_ImplVulkan_Init(&vulkanInit))
	{
		Throw(std::runtime_error("failed to initialise ImGui vulkan adapter"));
	}
	
	// Window scaling and style.
#if ANDROID
    const auto scaleFactor = 0.75 / GAndroidMagicScale;
#else
    const auto scaleFactor = 1.0;
#endif
	const auto fontSize = 16;

	UserInterface::SetStyle();
	ImGui::GetStyle().ScaleAllSizes(scaleFactor);
	
	// Upload ImGui fonts (use ImGuiFreeType for better font rendering, see https://github.com/ocornut/imgui/tree/master/misc/freetype).
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
	const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic() : GOption->locale == "zhCN" ? io.Fonts->GetGlyphRangesChineseFull() : io.Fonts->GetGlyphRangesDefault();
    
    std::vector<uint8_t> tmpData;
    if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/Roboto-Regular.ttf", tmpData))
    {
        void* dataSrc = IM_ALLOC(tmpData.size());
        std::memcpy(dataSrc, tmpData.data(), tmpData.size());
        io.Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()) , fontSize * scaleFactor, nullptr, glyphRange);
    }
	else
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

    if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/fa-regular-400.ttf", tmpData))
    {
        void* dataSrc = IM_ALLOC(tmpData.size());
        std::memcpy(dataSrc, tmpData.data(), tmpData.size());
        io.Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), fontSize * scaleFactor, &config, iconRange);
    }
    if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/fa-solid-900.ttf", tmpData))
    {
        void* dataSrc = IM_ALLOC(tmpData.size());
        std::memcpy(dataSrc, tmpData.data(), tmpData.size());
        io.Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), fontSize * scaleFactor, &config, iconRange);
    }
    if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/fa-brands-400.ttf", tmpData))
    {
        void* dataSrc = IM_ALLOC(tmpData.size());
        std::memcpy(dataSrc, tmpData.data(), tmpData.size());
        io.Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), fontSize * scaleFactor, &config, iconRange);
    }
    
	ImFontConfig configLocale;
	configLocale.MergeMode = true;
    if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/DroidSansFallback.ttf", tmpData))
    {
        void* dataSrc = IM_ALLOC(tmpData.size());
        std::memcpy(dataSrc, tmpData.data(), tmpData.size());
        io.Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), (fontSize + 2) * scaleFactor, &configLocale, glyphRange);
    }

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
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

void UserInterface::OnCreateSurface(const Vulkan::SwapChain& swapChain, const Vulkan::DepthBuffer& depthBuffer)
{
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	renderPass_->SetDebugName("ImGui Render Pass");
	
	for (const auto& imageView : swapChain.ImageViews())
	{
		uiFrameBuffers_.emplace_back(swapChain.Extent(), *imageView, *renderPass_, false);
	}
}

void UserInterface::OnDestroySurface()
{
	renderPass_.reset();
	uiFrameBuffers_.clear();
	for ( auto image : imTextureIdMap_)
	{
		ImGui_ImplVulkan_RemoveTexture(image.second);
	}
	imTextureIdMap_.clear();
}

VkDescriptorSet UserInterface::RequestImTextureId(uint32_t globalTextureId)
{
	auto texture = Assets::GlobalTexturePool::GetTextureImage(globalTextureId);
	if (texture == nullptr) return VK_NULL_HANDLE;
	
	if( imTextureIdMap_.find(globalTextureId) == imTextureIdMap_.end() )
	{
		imTextureIdMap_[globalTextureId] = ImGui_ImplVulkan_AddTexture(texture->Sampler().Handle(), texture->ImageView().Handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	return imTextureIdMap_[globalTextureId];
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

	ImVec4 activeColor = true ? ImVec4(0.42f, 0.45f, 0.5f, 1.00f) : ImVec4(0.28f, 0.45f, 0.70f, 1.00f);

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
    colors[ImGuiCol_CheckMark]              = activeColor;
    colors[ImGuiCol_SliderGrab]             = activeColor;
    colors[ImGuiCol_SliderGrabActive]       = activeColor;
    colors[ImGuiCol_Button]                 = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = activeColor;
    colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = activeColor;
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = activeColor;
    colors[ImGuiCol_SeparatorActive]        = activeColor;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.54f, 0.54f, 0.54f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = activeColor;
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.19f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = activeColor;
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.20f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = activeColor;
    colors[ImGuiCol_NavHighlight]           = activeColor;
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
		ImVec2 startPos = ImGui::GetMainViewport()->Pos;
		ImGui::GetForegroundDrawList()->AddRectFilled(startPos + ImVec2{x - size, y - size}, startPos + ImVec2{x + size, y + size}, Utilities::UI::Vec4ToImU32(color));
	});
}

void UserInterface::DrawLine(float fromx, float fromy, float tox, float toy, float size, glm::vec4 color)
{
	auxDrawRequest_.push_back( [=]() {
		ImVec2 startPos = ImGui::GetMainViewport()->Pos;
		ImGui::GetForegroundDrawList()->AddLine( startPos + ImVec2(fromx, fromy), startPos + ImVec2(tox, toy), Utilities::UI::Vec4ToImU32(color), size);
	});
}

void UserInterface::PreRender()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
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

void UserInterface::HandleEvent(const SDL_Event* event)
{
	ImGui_ImplSDL3_ProcessEvent(event);
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
	const ImVec2 pos = ImVec2(io.DisplaySize.x - distance, distance + 40);
	const ImVec2 posPivot = ImVec2(1.0f, 0.0f);

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
		// Colors
		const ImVec4 colHeader = ImVec4(0.8f, 0.8f, 1.0f, 1.0f);
		const ImVec4 colLabel  = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
		const ImVec4 colVal    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 colGood   = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
		const ImVec4 colWarn   = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
		const ImVec4 colBad    = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

		// Helper
		auto LabelVal = [&](const char* label, const char* fmt, auto... args) {
			ImGui::TextColored(colLabel, "%s", label);
			ImGui::SameLine();
			ImGui::TextColored(colVal, fmt, args...);
		};
		
		ImGui::TextColored(colHeader, "Resolution: %dx%d -> %dx%d", statistics.RenderSize.width, statistics.RenderSize.height, statistics.FramebufferSize.width, statistics.FramebufferSize.height);
		ImGui::TextColored(colLabel, "%s", statistics.Stats["gpu"].c_str());
		
		ImGui::Separator();

		// FPS
		ImVec4 fpsColor = statistics.FrameRate > 55.0f ? colGood : (statistics.FrameRate > 30.0f ? colWarn : colBad);
		ImGui::TextColored(colLabel, "Frame rate:"); ImGui::SameLine();
		ImGui::TextColored(fpsColor, "%.0f fps", statistics.FrameRate);
		
		LabelVal("Frame time:", "%.2f ms", statistics.FrameTime);

		ImGui::Separator();

		// Scene
		LabelVal("Node:", "%s", Utilities::metricFormatter(static_cast<double>(statistics.NodeCount), "").c_str());
		LabelVal("Instance:", "%s", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "").c_str());
		LabelVal("Texture:", "%d", statistics.TextureCount);

		ImGui::Separator();

		// GPU Stats
		auto& gpuDrivenStat = NextEngine::GetInstance()->GetScene().GetGpuDrivenStat();
		uint32_t instanceCount = gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount;
		uint32_t triangleCount = gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount;
		
		ImGui::TextColored(colHeader, "GPU Stats (Visible/Total):");
		LabelVal("Draws:", "%s / %s", Utilities::metricFormatter(static_cast<double>(instanceCount), "").c_str(), Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.ProcessedCount), "").c_str());
		LabelVal("Tris:", "%s / %s", Utilities::metricFormatter(static_cast<double>(triangleCount), "").c_str(), Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.TriangleCount), "").c_str());

		uint32_t mainTasks = TaskCoordinator::GetInstance()->GetMainTaskCount();
		uint32_t lowTasks = TaskCoordinator::GetInstance()->GetParralledTaskCount();
		uint32_t completeTasks = TaskCoordinator::GetInstance()->GetComleteTaskQueueCount();
		LabelVal("Tasks:", "%d / %d / %d", mainTasks, lowTasks, completeTasks);

		ImGui::Separator();
		
		// Timers
		auto times = gpuTimer->FetchAllTimes(4);
		float totalGpuTime = 0;
		for(auto& time : times) {
			if (std::get<2>(time) == 0) totalGpuTime += std::get<1>(time);
		}

		ImGui::TextColored(colHeader, "GPU Time (%.2fms):", totalGpuTime);
		for(auto& time : times)
		{
			float ms = std::get<1>(time);
			int depth = std::get<2>(time);
			std::string name = std::get<0>(time);

			ImGui::Indent(depth * 10.0f);
			ImGui::TextColored(depth == 0 ? colVal : colLabel, "%s: %.2fms", name.c_str(), ms);
			
			if (totalGpuTime > 0) {
				ImGui::SameLine(200); 

				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
				ImGui::PushStyleColor(ImGuiCol_Border, colLabel);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

				ImGui::ProgressBar(ms / (totalGpuTime > 0.001f ? totalGpuTime : 1.0f), ImVec2(50, ImGui::GetTextLineHeight()), "");
				
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
			}
			ImGui::Unindent(depth * 10.0f);
		}
		
		ImGui::TextColored(colHeader, "CPU Time (%.2fms):", gpuTimer->GetCpuTime("draw-frame"));
		// ImGui::Text("drawframe: %.2fms", gpuTimer->GetCpuTime("draw-frame"));
		LabelVal("  - hwquery:", "%.2fms", gpuTimer->GetCpuTime("hwquery"));
		LabelVal("  - fence:", "%.2fms", gpuTimer->GetCpuTime("fence"));
		LabelVal("  - present:", "%.2fms", gpuTimer->GetCpuTime("present"));
		
		ImGui::Separator();
		LabelVal("Frame:", "%d", statistics.TotalFrames);
		LabelVal("Time:", "%s", fmt::format("{:%H:%M:%S}", std::chrono::seconds(static_cast<long long>(statistics.RenderTime))).c_str());
	}
	ImGui::End();
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
