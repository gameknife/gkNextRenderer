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
	
	protected:

		RayQueryRenderer(const char* rendererType, const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
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

		virtual bool GetFocusDistance(float& distance) const override;
		virtual bool GetLastRaycastResult(Assets::RayCastResult& result) const override;
	
	private:
		void CreateOutputImage();

		Assets::RayCastResult cameraCenterCastResult_;
		
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
		std::unique_ptr<PipelineCommon::RayCastPipeline> raycastPipeline_;


		std::unique_ptr<Vulkan::Buffer> raycastInputBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> raycastInputBuffer_Memory_;

		std::unique_ptr<Vulkan::Buffer> raycastOutputBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> raycastOutputBuffer_Memory_;
	};

}
