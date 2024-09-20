#pragma once

#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"
#include "Vulkan/RayTracing/RayTracingProperties.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
		class FinalComposePipeline;
		class RayCastPipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
	class RenderImage;
}

namespace Vulkan::RayTracing
{
	class RayQueryPipeline;

	class RayQueryRenderer : public Vulkan::RayTracing::RayTraceBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayQueryRenderer);
	
	public:

		RayQueryRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayQueryRenderer();

		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures) override;
		
		void OnDeviceSet() override;
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		void BeforeNextFrame() override;
	
	private:
		void CreateOutputImage();

		
		
		std::unique_ptr<RenderImage> rtAccumulation_;
		std::unique_ptr<RenderImage> rtOutput_;
		std::unique_ptr<RenderImage> rtMotionVector_;
		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;
		std::unique_ptr<RenderImage> rtVisibility0_;
		std::unique_ptr<RenderImage> rtVisibility1_;

		std::unique_ptr<RenderImage> rtAlbedo_;
		std::unique_ptr<RenderImage> rtNormal_;

		std::unique_ptr<RenderImage> rtAdaptiveSample_;
		
		std::unique_ptr<RayQueryPipeline> rayTracingPipeline_;

		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineNonDenoiser_;
	};

}
