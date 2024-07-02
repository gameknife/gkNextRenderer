#include "UserInterface.hpp"
#include "SceneList.hpp"
#include "UserSettings.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/FrameBuffer.hpp"
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

#include "Utilities/FileHelper.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"

namespace
{
	void CheckVulkanResultCallback(const VkResult err)
	{
		if (err != VK_SUCCESS)
		{
			Throw(std::runtime_error(std::string("ImGui Vulkan error (") + Vulkan::ToString(err) + ")"));
		}
	}
}

UserInterface::UserInterface(
	Vulkan::CommandPool& commandPool, 
	const Vulkan::SwapChain& swapChain, 
	const Vulkan::DepthBuffer& depthBuffer,
	UserSettings& userSettings) :
	userSettings_(userSettings)
{
	const auto& device = swapChain.Device();
	const auto& window = device.Surface().Instance().Window();

	// Initialise descriptor pool and render pass for ImGui.
	const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
	};
	descriptorPool_.reset(new Vulkan::DescriptorPool(device, descriptorBindings, 1));
	renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD));

	// Initialise ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

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

	auto& io = ImGui::GetIO();

	// No ini file.
	io.IniFilename = nullptr;

	// Window scaling and style.
#if ANDROID
    const auto scaleFactor = 1.5;
#else
    const auto scaleFactor = 1.0;//window.ContentScale();
#endif


	ImGui::StyleColorsDark();
	ImGui::GetStyle().ScaleAllSizes(scaleFactor);

	// Upload ImGui fonts (use ImGuiFreeType for better font rendering, see https://github.com/ocornut/imgui/tree/master/misc/freetype).
	io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	if (!io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Cousine-Regular.ttf").c_str(), 13 * scaleFactor))
	{
		Throw(std::runtime_error("failed to load ImGui font"));
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
	ImGui_ImplVulkan_Shutdown();
#if !ANDROID
	ImGui_ImplGlfw_Shutdown();
#else
	ImGui_ImplAndroid_Shutdown();
#endif
	ImGui::DestroyContext();
}

void UserInterface::Render(VkCommandBuffer commandBuffer, const Vulkan::FrameBuffer& frameBuffer, const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer)
{
#if !ANDROID
	ImGui_ImplGlfw_NewFrame();
#else
	ImGui_ImplAndroid_NewFrame();
#endif
	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();

//#if !ANDROID
	DrawSettings();
//#endif
	DrawOverlay(statistics, gpuTimer);
	//ImGui::ShowStyleEditor();
	ImGui::Render();

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass_->Handle();
	renderPassInfo.framebuffer = frameBuffer.Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = renderPass_->SwapChain().Extent();
	renderPassInfo.clearValueCount = 0;
	renderPassInfo.pClearValues = nullptr;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
	vkCmdEndRenderPass(commandBuffer);
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

		ImGui::Text("Help");
		ImGui::Separator();
		ImGui::BulletText("F1: toggle Settings.");
		ImGui::BulletText("F2: toggle Statistics.");
		ImGui::NewLine();

		ImGui::Text("Scene");
		ImGui::Separator();
		ImGui::PushItemWidth(-1);
		ImGui::Combo("##SceneList", &Settings().SceneIndex, scenes.data(), static_cast<int>(scenes.size()));
		ImGui::PopItemWidth();
		ImGui::NewLine();

		ImGui::Text("Ray Tracing");
		ImGui::Separator();
		uint32_t min = 0, max = 7; //max bounce + 1 will off roulette. max bounce now is 6
		ImGui::SliderScalar("RR Start", ImGuiDataType_U32, &Settings().RR_MIN_DEPTH, &min, &max);
		ImGui::Checkbox("AdativeSample", &Settings().AdaptiveSample);
		ImGui::Checkbox("AntiAlias", &Settings().TAA);
		ImGui::SliderFloat("AdaptiveVariance", &Settings().AdaptiveVariance, 0.1f, 10.0f, "%.0f");
		ImGui::SliderInt("TemporalSteps", &Settings().AdaptiveSteps, 2, 16);

		
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
		
		ImGui::Text("Camera");
		ImGui::Separator();
		ImGui::SliderFloat("FoV", &Settings().FieldOfView, UserSettings::FieldOfViewMinValue, UserSettings::FieldOfViewMaxValue, "%.0f");
		ImGui::SliderFloat("Aperture", &Settings().Aperture, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Focus(cm)", &Settings().FocusDistance, 1.0f, 1000.0f, "%.1f");
		ImGui::SliderInt("SkyIdx", &Settings().SkyIdx, 0, 9);
		ImGui::SliderFloat("SkyRotation", &Settings().SkyRotation, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("SkyLum", &Settings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
		ImGui::SliderFloat("SunRotation", &Settings().SunRotation, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("SunLum", &Settings().SunLuminance, 0.0f, 2000.0f, "%.0f");
		
		
		ImGui::SliderFloat("PaperWhitNit", &Settings().PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
		
		ImGui::NewLine();

		ImGui::Text("Profiler");
		ImGui::Separator();
		ImGui::Checkbox("VisualDebug", &Settings().ShowVisualDebug);
		ImGui::SliderFloat("Scaling", &Settings().HeatmapScale, 0.10f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::NewLine();

		ImGui::Text("Performance");
		ImGui::Separator();
		ImGui::Checkbox("Use CheckerBoard", &Settings().UseCheckerBoardRendering);
		{
			uint32_t min = 0, max = 256;
			ImGui::SliderScalar("Temporal Frames", ImGuiDataType_U32, &Settings().TemporalFrames, &min, &max);		
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

		char buff[50];
		Utilities::metricFormatter(static_cast<double>(statistics.TriCount), buff, sizeof(buff), (void*)"");
		ImGui::Text("Tris: %s", buff);

		Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), buff, sizeof(buff), (void*)"");
		ImGui::Text("Instance: %s", buff);

		ImGui::Text("frametime: %.2fms", statistics.FrameTime);
		// auto fetch timer & display
		auto times = gpuTimer->FetchAllTimes();
		for(auto& time : times)
		{
			ImGui::Text("%s: %.2fms", std::get<0>(time).c_str(), std::get<1>(time));
		}
		
		ImGui::Text(" present: %.2fms", gpuTimer->GetCpuTime("present"));
		ImGui::Text(" fence: %.2fms", gpuTimer->GetCpuTime("sync-wait"));
		//ImGui::Text(" query: %.2fms", gpuTimer->GetCpuTime("query-wait"));
		ImGui::Text(" oidn: %.2fms", gpuTimer->GetCpuTime("OIDN"));
		
	}
	ImGui::End();
}
