#pragma once

#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
		class FinalComposePipeline;
		class VisualDebuggerPipeline;
	}

	class RenderImage;
	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
	class DeviceProcedures;
}

namespace Vulkan::RayTracing
{
	class RayTracingRenderer : public RayTraceBaseRenderer
	{
	public:
		VULKAN_NON_COPIABLE(RayTracingRenderer);
		RayTracingRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayTracingRenderer();
		
	protected:
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

		std::unique_ptr<RenderImage> rtDenoise0_;
		std::unique_ptr<RenderImage> rtDenoise1_;

		std::unique_ptr<RenderImage> rtAdaptiveSample_;
		
		std::unique_ptr<class RayTracingPipeline> rayTracingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<class ShaderBindingTable> shaderBindingTable_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineNonDenoiser_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineDenoiser_;
		std::unique_ptr<PipelineCommon::VisualDebuggerPipeline> visualDebugPipeline_;
	};

}
