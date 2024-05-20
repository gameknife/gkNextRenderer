#include "RayTracer.hpp"
#include "UserInterface.hpp"
#include "UserSettings.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Texture.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Device.hpp"
#include <iostream>
#include <sstream>
#include "curl/curl.h"

namespace
{
	const bool EnableValidationLayers =
#ifdef NDEBUG
		false;
#else
		true;
#endif
}

template <typename Renderer>
NextRendererApplication<Renderer>::NextRendererApplication(const UserSettings& userSettings, const Vulkan::WindowConfig& windowConfig, const VkPresentModeKHR presentMode) :
	Renderer(windowConfig, presentMode, EnableValidationLayers),
	userSettings_(userSettings)
{
	CheckFramebufferSize();
}

template <typename Renderer>
NextRendererApplication<Renderer>::~NextRendererApplication()
{
	scene_.reset();
}

template <typename Renderer>
Assets::UniformBufferObject NextRendererApplication<Renderer>::GetUniformBufferObject(const VkExtent2D extent) const
{
	const auto& init = cameraInitialSate_;

	Assets::UniformBufferObject ubo = {};
	ubo.ModelView = modelViewController_.ModelView();
	ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView), extent.width / static_cast<float>(extent.height), 0.1f, 10000.0f);
	ubo.Projection[1][1] *= -1; // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
	ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
	ubo.ProjectionInverse = glm::inverse(ubo.Projection);
	ubo.Aperture = userSettings_.Aperture;
	ubo.FocusDistance = userSettings_.FocusDistance;
	ubo.SkyRotation = userSettings_.SkyRotation;
	ubo.TotalNumberOfSamples = totalNumberOfSamples_;
	ubo.TotalFrames = totalFrames_;
	ubo.NumberOfSamples = numberOfSamples_;
	ubo.NumberOfBounces = userSettings_.NumberOfBounces;
	ubo.RandomSeed = rand();
	ubo.HasSky = init.HasSky;
	ubo.ShowHeatmap = userSettings_.ShowHeatmap;
	ubo.HeatmapScale = userSettings_.HeatmapScale;
	ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
	ubo.TemporalFrames = userSettings_.TemporalFrames;

	ubo.ColorPhi = userSettings_.ColorPhi;
	ubo.DepthPhi = userSettings_.DepthPhi;
	ubo.NormalPhi = userSettings_.NormalPhi;

	return ubo;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::SetPhysicalDeviceImpl(
	VkPhysicalDevice physicalDevice, 
	std::vector<const char*>& requiredExtensions,
	VkPhysicalDeviceFeatures& deviceFeatures, 
	void* nextDeviceFeatures)
{
	// Required extensions. windows only
#if WIN32
	requiredExtensions.insert(requiredExtensions.end(),
		{
			// VK_KHR_SHADER_CLOCK is required for heatmap
			VK_KHR_SHADER_CLOCK_EXTENSION_NAME
		});
#endif
	
	// Opt-in into mandatory device features.
	VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
	shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
	shaderClockFeatures.pNext = nextDeviceFeatures;
	shaderClockFeatures.shaderSubgroupClock = true;
	
	deviceFeatures.fillModeNonSolid = true;
	deviceFeatures.samplerAnisotropy = true;
	deviceFeatures.shaderInt64 = true;

	Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &shaderClockFeatures);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnDeviceSet()
{
	Renderer::OnDeviceSet();

	LoadScene(userSettings_.SceneIndex);

	Renderer::OnPostLoadScene();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CreateSwapChain()
{
	Renderer::CreateSwapChain();

	userInterface_.reset(new UserInterface(Renderer::CommandPool(), Renderer::SwapChain(), Renderer::DepthBuffer(), userSettings_));
	resetAccumulation_ = true;

	CheckFramebufferSize();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DeleteSwapChain()
{
	userInterface_.reset();

	Renderer::DeleteSwapChain();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DrawFrame()
{
	// Check if the scene has been changed by the user.
	if (sceneIndex_ != static_cast<uint32_t>(userSettings_.SceneIndex))
	{
		Renderer::Device().WaitIdle();
		DeleteSwapChain();
		Renderer::OnPreLoadScene();
		LoadScene(userSettings_.SceneIndex);
		Renderer::OnPostLoadScene();
		CreateSwapChain();
		return;
	}

	// Check if the accumulation buffer needs to be reset.
	if (resetAccumulation_ || 
		userSettings_.RequiresAccumulationReset(previousSettings_) || 
		!userSettings_.AccumulateRays)
	{
		totalNumberOfSamples_ = 0;
		resetAccumulation_ = false;
	}

	previousSettings_ = userSettings_;

	// Keep track of our sample count.
	numberOfSamples_ = glm::clamp(userSettings_.MaxNumberOfSamples - totalNumberOfSamples_, 0u, userSettings_.NumberOfSamples);
	totalNumberOfSamples_ += numberOfSamples_;
	totalFrames_ += 1;

	Renderer::DrawFrame();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
{
	// Record delta time between calls to Render.
	const auto prevTime = time_;
	time_ = Renderer::Window().GetTime();
	const auto timeDelta = time_ - prevTime;

	// Update the camera position / angle.
	resetAccumulation_ = modelViewController_.UpdateCamera(cameraInitialSate_.ControlSpeed, timeDelta);

	// Check the current state of the benchmark, update it for the new frame.
	CheckAndUpdateBenchmarkState(prevTime);
	
	Renderer::denoiseIteration_ = userSettings_.DenoiseIteration;
	Renderer::checkerboxRendering_ = userSettings_.UseCheckerBoardRendering;
	
	// Render the scene
	Renderer::Render(commandBuffer, imageIndex);

	// Render the UI
	Statistics stats = {};
	stats.FramebufferSize = Renderer::Window().FramebufferSize();
	stats.FrameRate = static_cast<float>(1 / timeDelta);

	stats.CamPosX = modelViewController_.Position()[0];
	stats.CamPosY = modelViewController_.Position()[1];
	stats.CamPosZ = modelViewController_.Position()[2];

	if (userSettings_.IsRayTraced)
	{
		const auto extent = Renderer::SwapChain().Extent();

		stats.RayRate = static_cast<float>(
			double(extent.width*extent.height)*numberOfSamples_
			/ (timeDelta * 1000000000));

		stats.TotalSamples = totalNumberOfSamples_;
	}

	userInterface_->Render(commandBuffer, Renderer::SwapChainFrameBuffer(imageIndex), stats);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnKey(int key, int scancode, int action, int mods)
{
	if (userInterface_->WantsToCaptureKeyboard())
	{
		return;
	}

	if (action == GLFW_PRESS)
	{		
		switch (key)
		{
		case GLFW_KEY_ESCAPE: Renderer::Window().Close(); break;
		default: break;
		}

		// Settings (toggle switches)
		if (!userSettings_.Benchmark)
		{
			switch (key)
			{
			case GLFW_KEY_F1: userSettings_.ShowSettings = !userSettings_.ShowSettings; break;
			case GLFW_KEY_F2: userSettings_.ShowOverlay = !userSettings_.ShowOverlay; break;
			case GLFW_KEY_R: userSettings_.IsRayTraced = !userSettings_.IsRayTraced; break;
			case GLFW_KEY_H: userSettings_.ShowHeatmap = !userSettings_.ShowHeatmap; break;
			default: break;
			}
		}
	}

	// Camera motions
	if (!userSettings_.Benchmark)
	{
		resetAccumulation_ |= modelViewController_.OnKey(key, scancode, action, mods);
	}
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnCursorPosition(const double xpos, const double ypos)
{
	if (!Renderer::HasSwapChain() ||
		userSettings_.Benchmark ||
		userInterface_->WantsToCaptureKeyboard() || 
		userInterface_->WantsToCaptureMouse())
	{
		return;
	}

	// Camera motions
	resetAccumulation_ |= modelViewController_.OnCursorPosition(xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnMouseButton(const int button, const int action, const int mods)
{
	if (!Renderer::HasSwapChain() || 
		userSettings_.Benchmark ||
		userInterface_->WantsToCaptureMouse())
	{
		return;
	}

	// Camera motions
	resetAccumulation_ |= modelViewController_.OnMouseButton(button, action, mods);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnScroll(const double xoffset, const double yoffset)
{
	if (!Renderer::HasSwapChain() ||
		userSettings_.Benchmark ||
		userInterface_->WantsToCaptureMouse())
	{
		return;
	}

	const auto prevFov = userSettings_.FieldOfView;
	userSettings_.FieldOfView = std::clamp(
		static_cast<float>(prevFov - yoffset),
		UserSettings::FieldOfViewMinValue,
		UserSettings::FieldOfViewMaxValue);

	resetAccumulation_ = prevFov != userSettings_.FieldOfView;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::LoadScene(const uint32_t sceneIndex)
{
	std::vector<Assets::Model> models;
	std::vector<Assets::Texture> textures;

	// texture id 0: global sky
	textures.push_back(Assets::Texture::LoadHDRTexture("../assets/textures/StinsonBeach.hdr", Vulkan::SamplerConfig()));
	
	SceneList::AllScenes[sceneIndex].second(cameraInitialSate_, models, textures);

	// If there are no texture, add a dummy one. It makes the pipeline setup a lot easier.
	if (textures.empty())
	{
		textures.push_back(Assets::Texture::LoadTexture("../assets/textures/white.png", Vulkan::SamplerConfig()));
	}
	
	scene_.reset(new Assets::Scene(Renderer::CommandPool(), std::move(models), std::move(textures), false));
	sceneIndex_ = sceneIndex;

	userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
	userSettings_.Aperture = cameraInitialSate_.Aperture;
	userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;

	modelViewController_.Reset(cameraInitialSate_.ModelView);

	periodTotalFrames_ = 0;
	resetAccumulation_ = true;
}

template <>
void NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>::LoadScene(const uint32_t sceneIndex)
{
	std::vector<Assets::Model> models;
	std::vector<Assets::Texture> textures;

	// texture id 0: global sky
	textures.push_back(Assets::Texture::LoadHDRTexture("../assets/textures/StinsonBeach.hdr", Vulkan::SamplerConfig()));
	
	SceneList::AllScenes[sceneIndex].second(cameraInitialSate_, models, textures);

	// If there are no texture, add a dummy one. It makes the pipeline setup a lot easier.
	if (textures.empty())
	{
		textures.push_back(Assets::Texture::LoadTexture("../assets/textures/white.png", Vulkan::SamplerConfig()));
	}
	
	scene_.reset(new Assets::Scene(CommandPool(), std::move(models), std::move(textures), true));
	sceneIndex_ = sceneIndex;

	userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
	userSettings_.Aperture = cameraInitialSate_.Aperture;
	userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;

	modelViewController_.Reset(cameraInitialSate_.ModelView);

	periodTotalFrames_ = 0;
	resetAccumulation_ = true;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckAndUpdateBenchmarkState(double prevTime)
{
	if (!userSettings_.Benchmark)
	{
		return;
	}
	
	// Initialise scene benchmark timers
	if (periodTotalFrames_ == 0)
	{
		std::cout << std::endl;
		std::cout << "Renderer: " << Renderer::StaticClass() << std::endl;
		std::cout << "Benchmark: Start scene #" << sceneIndex_ << " '" << SceneList::AllScenes[sceneIndex_].first << "'" << std::endl;
		sceneInitialTime_ = time_;
		periodInitialTime_ = time_;
	}

	// Print out the frame rate at regular intervals.
	{
		const double period = 5;
		const double prevTotalTime = prevTime - periodInitialTime_;
		const double totalTime = time_ - periodInitialTime_;

		if (periodTotalFrames_ != 0 && static_cast<uint64_t>(prevTotalTime / period) != static_cast<uint64_t>(totalTime / period))
		{
			std::cout << "Benchmark: " << periodTotalFrames_ / totalTime << " fps" << std::endl;
			periodInitialTime_ = time_;
			periodTotalFrames_ = 0;
		}

		periodTotalFrames_++;
	}

	// If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
	{
		const bool timeLimitReached = periodTotalFrames_ != 0 && Renderer::Window().GetTime() - sceneInitialTime_ > userSettings_.BenchmarkMaxTime;
		const bool sampleLimitReached = numberOfSamples_ == 0;

		if (timeLimitReached || sampleLimitReached)
		{
			if (!userSettings_.BenchmarkNextScenes || static_cast<size_t>(userSettings_.SceneIndex) == SceneList::AllScenes.size() - 1)
			{
				std::cout << "Sending benchmark to perf server..." << std::endl;
				// upload from curl
				CURL *curl;
				CURLcode res;
				curl_global_init(CURL_GLOBAL_ALL);
				curl = curl_easy_init();
				if(curl)
				{
					curl_easy_setopt(curl, CURLOPT_URL, "http://gameknife.site:60010/rt_benchmark");
					curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "name=daniel&project=curl");

					/* Perform the request, res gets the return code */
					res = curl_easy_perform(curl);
					/* Check for errors */
					if(res != CURLE_OK)
						fprintf(stderr, "curl_easy_perform() failed: %s\n",
							curl_easy_strerror(res));
 
					/* always cleanup */
					curl_easy_cleanup(curl);
				}
				
				Renderer::Window().Close();
			}

			std::cout << std::endl;
			userSettings_.SceneIndex += 1;
		}
	}
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckFramebufferSize() const
{
	// Check the framebuffer size when requesting a fullscreen window, as it's not guaranteed to match.
	const auto& cfg = Renderer::Window().Config();
	const auto fbSize = Renderer::Window().FramebufferSize();
	
	if (userSettings_.Benchmark && cfg.Fullscreen && (fbSize.width != cfg.Width || fbSize.height != cfg.Height))
	{
		std::ostringstream out;
		out << "framebuffer fullscreen size mismatch (requested: ";
		out << cfg.Width << "x" << cfg.Height;
		out << ", got: ";
		out << fbSize.width << "x" << fbSize.height << ")";
		
		Throw(std::runtime_error(out.str()));
	}
}

// export it 
template class NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>;
template class NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>;
template class NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>;
template class NextRendererApplication<Vulkan::VulkanBaseRenderer>;