#pragma once
#include "Vulkan/Vulkan.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace ImStudio
{
	struct GUI;
}

namespace Vulkan
{
	class CommandPool;
	class DepthBuffer;
	class DescriptorPool;
	class FrameBuffer;
	class RenderPass;
	class SwapChain;
	class VulkanGpuTimer;
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
	float CamPosX;
	float CamPosY;
	float CamPosZ;
	uint32_t TriCount;
	uint32_t InstanceCount;
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
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer,
		UserSettings& userSettings);
	~UserInterface();

	void Render(VkCommandBuffer commandBuffer, const Vulkan::FrameBuffer& frameBuffer, const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	UserSettings& Settings() { return userSettings_; }

private:

	void DrawSettings();
	void DrawOverlay(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer);
	void DrawIndicator(uint32_t frameCount);
	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	UserSettings& userSettings_;

	std::unique_ptr<ImStudio::GUI> editorGUI_;
};
