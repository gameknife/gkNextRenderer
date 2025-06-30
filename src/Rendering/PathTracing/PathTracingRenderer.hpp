#pragma once

#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"

#if WITH_OIDN
#include <oidn.hpp>
#endif

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
	class PathTracingPipeline;

	class PathTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(PathTracingRenderer);
	
	public:

		PathTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		virtual ~PathTracingRenderer();

		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);// override;
		
		void OnDeviceSet() override;
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		void BeforeNextFrame() override;
	
	private:
		void CreateOutputImage(const VkExtent2D& extent);

		// individual textures
		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;
		
		std::unique_ptr<PathTracingPipeline> rayTracingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipelineSpec_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineNonDenoiser_;

#if WITH_OIDN
		std::unique_ptr<RenderImage> rtDenoise0_;
		std::unique_ptr<RenderImage> rtDenoise1_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineDenoiser_;
		oidn::DeviceRef device;
		oidn::FilterRef filter;
#endif
	};

}
