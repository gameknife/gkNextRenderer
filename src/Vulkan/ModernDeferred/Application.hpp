#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Application.hpp"

#include <vector>
#include <memory>



namespace Assets
{
	class Scene;
	class UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::ModernDeferred
{
	class ModernDeferredRenderer : public Vulkan::VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(ModernDeferredRenderer)
		
		ModernDeferredRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		~ModernDeferredRenderer();

		// void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
		// 	std::vector<const char*>& requiredExtensions,
		// 	VkPhysicalDeviceFeatures& deviceFeatures,
		// 	void* nextDeviceFeatures) override;
		//
		// void OnDeviceSet() override;
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class VisibilityPipeline> visibilityPipeline_;
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		std::unique_ptr<Image> miniGBufferImage_;
		std::unique_ptr<DeviceMemory> miniGBufferImageMemory_;
		std::unique_ptr<ImageView> miniGBufferImageView_;

		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;
	};

}
