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

namespace Vulkan::LegacyDeferred
{
	class LegacyDeferredRenderer : public Vulkan::VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(LegacyDeferredRenderer)
		
		LegacyDeferredRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		~LegacyDeferredRenderer();
		
		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures) override;

		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class GBufferPipeline> gbufferPipeline_;
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		// simple gbuffer, shading mode may increase
		// gbuffer0 albedo, gbuffer1 normal+roughness. gbuffer2 reserved
		// simulating total size of 32 + 128 + 32 = 192 bits
		std::unique_ptr<Image> gbuffer0BufferImage_;
		std::unique_ptr<DeviceMemory> gbufferBuffer0ImageMemory_;
		std::unique_ptr<ImageView> gbufferBuffer0ImageView_;
		
		std::unique_ptr<Image> gbuffer1BufferImage_;
		std::unique_ptr<DeviceMemory> gbufferBuffer1ImageMemory_;
		std::unique_ptr<ImageView> gbufferBuffer1ImageView_;

		std::unique_ptr<Image> gbuffer2BufferImage_;
		std::unique_ptr<DeviceMemory> gbufferBuffer2ImageMemory_;
		std::unique_ptr<ImageView> gbufferBuffer2ImageView_;
		
		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;
	};

}
