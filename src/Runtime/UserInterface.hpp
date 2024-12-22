#pragma once
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include "Vulkan/Vulkan.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/vec4.hpp>

class NextEngine;

namespace Assets
{
	class Scene;
}

namespace Vulkan
{
	class Window;
	class CommandPool;
	class DepthBuffer;
	class DescriptorPool;
	class FrameBuffer;
	class RenderPass;
	class SwapChain;
	class VulkanGpuTimer;
	class RenderImage;
}

struct UserSettings;

struct Statistics final
{
	VkExtent2D FramebufferSize;
	float FrameRate;
	float FrameTime;
	float RayRate;
	uint32_t TotalSamples;
	uint32_t TotalFrames;
	double RenderTime;
	uint32_t TriCount;
	uint32_t InstanceCount;
	uint32_t NodeCount;
	uint32_t TextureCount;
	uint32_t ComputePassCount;
	bool LoadingStatus;

	mutable std::unordered_map< std::string, std::string> Stats;
};

class UserInterface final
{
public:

	VULKAN_NON_COPIABLE(UserInterface)

	UserInterface(
		NextEngine* engine,
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer,
		UserSettings& userSettings,
		std::function<void()> funcPreConfig,
		std::function<void()> funcInit);
	~UserInterface();

	void PreRender();
	void Render(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, Assets::Scene* scene);
	void PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	UserSettings& Settings() { return userSettings_; }

	void OnCreateSurface(const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer);
	void OnDestroySurface();

	VkDescriptorSet RequestImTextureId(uint32_t globalTextureId);
	VkDescriptorSet RequestImTextureByName(const std::string& name);
	
	static void SetStyle();

	void DrawPoint(float x, float y, float size, glm::vec4 color);
	void DrawLine(float fromx, float fromy,float tox, float toy, float size, glm::vec4 color);

private:
	NextEngine& GetEngine() {return *engine_;}
	
	void DrawOverlay(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer);
	void DrawIndicator(uint32_t frameCount);
	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
	UserSettings& userSettings_;	
	
	std::unordered_map<uint32_t, VkDescriptorSet> imTextureIdMap_;
	std::vector< std::function<void ()> > auxDrawRequest_;

	NextEngine* engine_;
};