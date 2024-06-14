#pragma once

#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"
#include "Vulkan/RayTracing/RayTracingProperties.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
}

namespace Vulkan::RayTracing
{
	class RayQueryPipeline;

	class RayQueryRenderer : public Vulkan::RayTracing::RayTraceBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayQueryRenderer);
	
	protected:

		RayQueryRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayQueryRenderer();

		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures) override;
		
		void OnDeviceSet() override;
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
	
	private:
		void CreateOutputImage();
		
		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;
		
		std::unique_ptr<RayQueryPipeline> rayTracingPipeline_;
	};

}
