#pragma once

#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class ZeroBindPipeline;
		class ZeroBindWithTLASPipeline;
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
		// individual textures
		std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> rayTracingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipelineNonDenoiser_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;

		uint32_t prevSingleDiffuseId_{};
		uint32_t prevSingleSpecularId_{};
		uint32_t prevSingleAlbedoId_{};
	};

}
