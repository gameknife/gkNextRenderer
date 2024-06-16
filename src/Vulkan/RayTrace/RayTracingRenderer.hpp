#pragma once

#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
	}

	class RenderImage;
	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
}

namespace Vulkan::RayTracing
{
	class RayTracingRenderer : public RayTraceBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayTracingRenderer);
	
	protected:

		RayTracingRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayTracingRenderer();

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

		std::unique_ptr<RenderImage> rtAccumulation_;
		std::unique_ptr<RenderImage> rtOutput_;
		std::unique_ptr<RenderImage> rtMotionVector_;
		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;
		std::unique_ptr<RenderImage> rtVisibility0_;
		std::unique_ptr<RenderImage> rtVisibility1_;

		std::unique_ptr<class RayTracingPipeline> rayTracingPipeline_;
		std::unique_ptr<class PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<class ShaderBindingTable> shaderBindingTable_;
	};

}
