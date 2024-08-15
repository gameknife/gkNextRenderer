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

#include "Options.hpp"
#include "Editor/EditorMain.h"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Editor/IconsFontAwesome6.h"

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

UserInterface::UserInterface(
	Vulkan::CommandPool& commandPool, 
	const Vulkan::SwapChain& swapChain, 
	const Vulkan::DepthBuffer& depthBuffer,
	UserSettings& userSettings,
	Vulkan::RenderImage& viewportImage) :
	userSettings_(userSettings),
	swapChain_(swapChain)
{
	editorGUI_.reset(new Editor::GUI());
	
	const auto& device = swapChain.Device();
	const auto& window = device.Surface().Instance().Window();

	// Initialise descriptor pool and render pass for ImGui.
	const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
	};
	descriptorPool_.reset(new Vulkan::DescriptorPool(device, descriptorBindings, swapChain.MinImageCount()));
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
	
	const auto& debugUtils = device.DebugUtils();
	debugUtils.SetObjectName(renderPass_->Handle(), "UI RenderPass");
	
	for (const auto& imageView : swapChain.ImageViews())
	{
		uiFrameBuffers_.emplace_back(*imageView, *renderPass_, false);
		debugUtils.SetObjectName(uiFrameBuffers_.back().Handle(), "UI FrameBuffer");
	}
	
	// Initialise ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	
	auto& io = ImGui::GetIO();
	// No ini file.
	io.IniFilename = "imgui.ini";
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	if(GOption->Editor) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	
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
	vulkanInit.CheckVkResultFn = CheckVulkanResultCallback;

	if (!ImGui_ImplVulkan_Init(&vulkanInit, renderPass_->Handle()))
	{
		Throw(std::runtime_error("failed to initialise ImGui vulkan adapter"));
	}
	
	// Window scaling and style.
#if ANDROID
    const auto scaleFactor = 1.5;
#elif __APPLE__
	const auto scaleFactor = 1.0;
#else
    const auto scaleFactor = window.ContentScale();
#endif


	//ImGui::StyleColorsDark();
	MainWindowStyle();
	ImGui::GetStyle().ScaleAllSizes(scaleFactor);


	// Upload ImGui fonts (use ImGuiFreeType for better font rendering, see https://github.com/ocornut/imgui/tree/master/misc/freetype).
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
	const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic() : GOption->locale == "zhCN" ? io.Fonts->GetGlyphRangesChineseFull() : io.Fonts->GetGlyphRangesDefault();
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-Regular.ttf").c_str(), 14 * scaleFactor, nullptr, glyphRange ))
	{
		Throw(std::runtime_error("failed to load ImGui Text font"));
	}

	ImFontConfig configLocale;
	configLocale.MergeMode = true;
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/MicrosoftYaHeiMono.ttf").c_str(), 14 * scaleFactor, &configLocale, glyphRange ))
	{
		Throw(std::runtime_error("failed to load ImGui Text font"));
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
	
	Vulkan::SingleTimeCommands::Submit(commandPool, [&viewportImage] (VkCommandBuffer commandBuffer)
	{
		if (!ImGui_ImplVulkan_CreateFontsTexture())
		{
			Throw(std::runtime_error("failed to create ImGui font textures"));
		}
	});

	viewportTextureId_ = ImGui_ImplVulkan_AddTexture( viewportImage.Sampler().Handle(), viewportImage.GetImageView().Handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	viewportSize_ = ImVec2(static_cast<float>(viewportImage.GetImage().Extent().width), static_cast<float>(viewportImage.GetImage().Extent().height));

	firstRun = true;

	editorGUI_->fontIcon_ = fontIcon_;
	editorGUI_->bigIcon_ = fontBigIcon_;
}

UserInterface::~UserInterface()
{
	uiFrameBuffers_.clear();
	
	ImGui_ImplVulkan_RemoveTexture(viewportTextureId_);
	
	editorGUI_.reset();
	
	ImGui_ImplVulkan_Shutdown();
#if !ANDROID
	ImGui_ImplGlfw_Shutdown();
#else
	ImGui_ImplAndroid_Shutdown();
#endif
	ImGui::DestroyContext();
}

const float toolbarSize = 50;
const float toolbarIconWidth = 32;
const float toolbarIconHeight = 32;
const float titleBarHeight = 55;
const float footBarHeight = 40;
float menuBarHeight = 0;


ImGuiID UserInterface::DockSpaceUI()
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
	menuBarHeight = ImGui::GetCurrentWindow()->MenuBarHeight();

	ImGui::DockSpace(dockMain, ImVec2(0,0), ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
	ImGui::PopStyleVar(3);

	return dockMain;
}

void UserInterface::ToolbarUI()
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
	ImGui::SetCursorPosY(14);
	ImGui::Combo("##Render", &item, "RTPipe\0ModernDeferred\0LegacyDeferred\0RayQuery\0HybirdRender\0\0");ImGui::SameLine();
	ImGui::EndGroup();ImGui::SameLine();


	ImGui::BeginGroup();ImGui::SameLine(50);
	ImGui::PushFont(fontIcon_);
	ImGui::Button(ICON_FA_FILE_IMPORT, ImVec2(toolbarIconWidth, toolbarIconHeight));ImGui::SameLine();
	ImGui::PopFont();
	ImGui::EndGroup();
	
	ImGui::End();
}

void UserInterface::Render(VkCommandBuffer commandBuffer, uint32_t imageIdx, const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, const Assets::Scene* scene)
{
	auto& io = ImGui::GetIO();
	
	ImGui_ImplVulkan_NewFrame();
#if !ANDROID
	ImGui_ImplGlfw_NewFrame();
#else
	ImGui_ImplAndroid_NewFrame();
#endif
	ImGui::NewFrame();
	

	if( GOption->Editor )
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGuiID id = DockSpaceUI();
		ToolbarUI();
		ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);
		swapChain_.UpdateEditorViewport(Utilities::Math::floorToInt(node->Pos.x - viewport->Pos.x), Utilities::Math::floorToInt(node->Pos.y - viewport->Pos.y), Utilities::Math::ceilToInt(node->Size.x), Utilities::Math::ceilToInt(node->Size.y));
		MainWindowGUI(*editorGUI_, scene, statistics, id, firstRun);
	}
	else
	{
		DrawSettings();
		DrawOverlay(statistics, gpuTimer);
	}

	if( statistics.LoadingStatus ) DrawIndicator(static_cast<uint32_t>(std::floor(statistics.RenderTime * 2)));
	
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
		// TODO for OpenGL: restore current GL context.
	}

	firstRun = false;
}

bool UserInterface::WantsToCaptureKeyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool UserInterface::WantsToCaptureMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
}

void UserInterface::DrawSettings()
{
	if (!Settings().ShowSettings)
	{
		return;
	}

	const float distance = 10.0f;
	const ImVec2 pos = ImVec2(distance, distance);
	const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);

	const auto flags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Settings", &Settings().ShowSettings, flags))
	{
		std::vector<const char*> scenes;
		for (const auto& scene : SceneList::AllScenes)
		{
			scenes.push_back(scene.first.c_str());
		}

		std::vector<const char*> camerasList;
		for (const auto& cam : Settings().cameras)
		{
			camerasList.emplace_back(cam.name.c_str());
		}

		ImGui::Text("%s", LOCTEXT("Help"));
		ImGui::Separator();
		ImGui::BulletText("%s", LOCTEXT("F1: toggle Settings."));
		ImGui::BulletText("%s", LOCTEXT("F2: toggle Statistics."));
		ImGui::BulletText("%s", LOCTEXT("SPACE: hold to auto focus."));
		ImGui::BulletText("%s", LOCTEXT("DropFile: if glb file, load it."));
		ImGui::NewLine();

		ImGui::Text("%s", LOCTEXT("Scene"));
		ImGui::Separator();
		ImGui::PushItemWidth(-1);
		ImGui::Combo("##SceneList", &Settings().SceneIndex, scenes.data(), static_cast<int>(scenes.size()));
		ImGui::PopItemWidth();
		ImGui::NewLine();

		ImGui::Text("%s", LOCTEXT("Ray Tracing"));
		ImGui::Separator();
		uint32_t min = 0, max = 7; //max bounce + 1 will off roulette. max bounce now is 6
		ImGui::SliderScalar(LOCTEXT("RR Start"), ImGuiDataType_U32, &Settings().RR_MIN_DEPTH, &min, &max);
		ImGui::Checkbox(LOCTEXT("AdaptiveSample"), &Settings().AdaptiveSample);
		ImGui::Checkbox(LOCTEXT("AntiAlias"), &Settings().TAA);
		ImGui::SliderFloat(LOCTEXT("AdaptiveVariance"), &Settings().AdaptiveVariance, 0.1f, 10.0f, "%.0f");
		ImGui::SliderInt(LOCTEXT("TemporalSteps"), &Settings().AdaptiveSteps, 2, 16);

		
		ImGui::NewLine();

		// min = 1, max = 128;
		// ImGui::SliderScalar("Samples", ImGuiDataType_U32, &Settings().NumberOfSamples, &min, &max);
		// min = 1, max = 32;
		// ImGui::SliderScalar("Bounces", ImGuiDataType_U32, &Settings().NumberOfBounces, &min, &max);
		// ImGui::NewLine();

		#if WITH_OIDN
			ImGui::Text("Denoiser");
			ImGui::Separator();
			ImGui::Checkbox("Use OIDN", &Settings().Denoiser);
			ImGui::NewLine();
		#endif

		int prevCameraIdx = Settings().CameraIdx;
		ImGui::Text("%s", LOCTEXT("Camera"));
		ImGui::Separator();
		ImGui::PushItemWidth(-1);
		ImGui::Combo("##CameraList", &Settings().CameraIdx, camerasList.data(), static_cast<int>(camerasList.size()));
		ImGui::PopItemWidth();
		ImGui::NewLine();

		if(prevCameraIdx != Settings().CameraIdx)
		{
			auto &cam = Settings().cameras[Settings().CameraIdx];
			Settings().FieldOfView = cam.FieldOfView;
			Settings().Aperture = cam.Aperture;
			Settings().FocusDistance = cam.FocalDistance;
		}

		ImGui::SliderFloat(LOCTEXT("FoV"), &Settings().FieldOfView, UserSettings::FieldOfViewMinValue, UserSettings::FieldOfViewMaxValue, "%.0f");
		ImGui::SliderFloat(LOCTEXT("Aperture"), &Settings().Aperture, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(LOCTEXT("Focus(cm)"), &Settings().FocusDistance, 0.001f, 1000.0f, "%.3f");
		
		ImGui::SliderInt(LOCTEXT("SkyIdx"), &Settings().SkyIdx, 0, 10);
		ImGui::SliderFloat(LOCTEXT("SkyRotation"), &Settings().SkyRotation, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat(LOCTEXT("SkyLum"), &Settings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
		ImGui::SliderFloat(LOCTEXT("SunRotation"), &Settings().SunRotation, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat(LOCTEXT("SunLum"), &Settings().SunLuminance, 0.0f, 2000.0f, "%.0f");

		ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &Settings().PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
		
		ImGui::NewLine();

		ImGui::Text("%s", LOCTEXT("Profiler"));
		ImGui::Separator();
		ImGui::Checkbox(LOCTEXT("ShaderTime"), &Settings().ShowVisualDebug);
		ImGui::SliderFloat(LOCTEXT("Scaling"), &Settings().HeatmapScale, 0.10f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::NewLine();

		ImGui::Text("%s", LOCTEXT("Performance"));
		ImGui::Separator();
		//ImGui::Checkbox("Use CheckerBoard", &Settings().UseCheckerBoardRendering);
		{
			uint32_t min = 0, max = 256;
			ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &Settings().TemporalFrames, &min, &max);		
		}
		ImGui::NewLine();
	}
	ImGui::End();
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
	const ImVec2 pos = ImVec2(io.DisplaySize.x * 0.75 - distance, distance);
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
		ImGui::Text("Campos:  %.2f %.2f %.2f", statistics.CamPosX, statistics.CamPosY, statistics.CamPosZ);
		
		ImGui::Text("Tris: %s", Utilities::metricFormatter(static_cast<double>(statistics.TriCount), "").c_str());

		
		ImGui::Text("Instance: %s", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "").c_str());
		ImGui::Text("Texture: %d", statistics.TextureCount);

		ImGui::Text("frametime: %.2fms", statistics.FrameTime);
		// auto fetch timer & display
		auto times = gpuTimer->FetchAllTimes();
		for(auto& time : times)
		{
			ImGui::Text("%s: %.2fms", std::get<0>(time).c_str(), std::get<1>(time));
		}
		
		ImGui::Text(" present: %.2fms", gpuTimer->GetCpuTime("present"));
		ImGui::Text(" fence: %.2fms", gpuTimer->GetCpuTime("sync-wait"));
		ImGui::Text(" query: %.2fms", gpuTimer->GetCpuTime("query-wait"));
		ImGui::Text(" cpugpu-io: %.2fms", gpuTimer->GetCpuTime("cpugpu-io"));
		
		ImGui::Text(" oidn: %.2fms", gpuTimer->GetCpuTime("OIDN"));
		
		ImGui::Text("Frame: %d", statistics.TotalFrames);
		
		ImGui::Text("Time: %s", fmt::format("{:%H:%M:%S}", std::chrono::seconds(static_cast<long long>(statistics.RenderTime))).c_str());
	}
	ImGui::End();

	if( Settings().AutoFocus )
	{
		// draw a center dot with imgui
		auto io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
		ImGui::SetNextWindowSize(ImVec2(8, 8));
	
		// set border color
		ImGui::PushStyleColor(ImGuiCol_Border, !Settings().AutoFocus ? ImVec4(1,1,1,1) : Settings().HitResult.Hitted ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f): ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1,1));
		ImGui::Begin("CenterDot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		//ImGui::Text(" ");
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
		ImGui::SetNextWindowSize(ImVec2(1, 1));
	
		// set border color
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f + 10.0f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
		ImGui::SetNextWindowSize(ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
		ImGui::Begin("HitInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Text("%.1fm\nInst: %d\nMat: %d", Settings().HitResult.T, Settings().HitResult.InstanceId, Settings().HitResult.MaterialId);
		ImGui::End();
		ImGui::PopStyleColor();
		
	}
}

void UserInterface::DrawIndicator(uint32_t frameCount)
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
