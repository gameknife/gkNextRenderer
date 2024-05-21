#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Application.hpp"

#include <vector>
#include <memory>

namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline;
}

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
		std::unique_ptr<class PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		std::unique_ptr<Image> visibilityBufferImage_;
		std::unique_ptr<DeviceMemory> visibilityBufferImageMemory_;
		std::unique_ptr<ImageView> visibilityBufferImageView_;

		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;

		std::unique_ptr<Image> accumulateImage_;
		std::unique_ptr<DeviceMemory> accumulateImageMemory_;
		std::unique_ptr<ImageView> accumulateImageView_;

		std::unique_ptr<Image> accumulateImage1_;
		std::unique_ptr<DeviceMemory> accumulateImage1Memory_;
		std::unique_ptr<ImageView> accumulateImage1View_;
		
		std::unique_ptr<Image> motionVectorImage_;
		std::unique_ptr<DeviceMemory> motionVectorImageMemory_;
		std::unique_ptr<ImageView> motionVectorImageView_;
		
	};

}
