#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include "Vulkan/Vulkan.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace Assets
{
	class Scene;
}
namespace ImStudio
{
	struct GUI;
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
		UserSettings& userSettings,
		Vulkan::RenderImage& viewportImage);
	~UserInterface();

	void Render(VkCommandBuffer commandBuffer, uint32_t imageIdx, const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, const Assets::Scene* scene);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	UserSettings& Settings() { return userSettings_; }

private:

	ImGuiID DockSpaceUI();
	void ToolbarUI();
	
	const Vulkan::SwapChain& swapChain_;
	void DrawSettings();
	void DrawOverlay(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer);
	void DrawIndicator(uint32_t frameCount);
	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
	UserSettings& userSettings_;

	std::unique_ptr<ImStudio::GUI> editorGUI_;

	VkDescriptorSet viewportTextureId_;
	ImVec2 viewportSize_;

	ImFont* fontBigIcon_;
	ImFont* fontIcon_;

	bool firstRun;
};
