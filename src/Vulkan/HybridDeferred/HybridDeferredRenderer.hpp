#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"
#include <vector>
#include <memory>



namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline;
}

namespace Vulkan::ModernDeferred
{
	class VisibilityPipeline;
}
namespace Assets
{
	class Scene;
	class UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::HybridDeferred
{
	class HybridDeferredRenderer : public RayTracing::RayTraceBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(HybridDeferredRenderer)
		
		HybridDeferredRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		~HybridDeferredRenderer();
		
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<ModernDeferred::VisibilityPipeline> visibilityPipeline_;
		std::unique_ptr<class HybridShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<class PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		std::unique_ptr<Image> visibilityBufferImage_;
		std::unique_ptr<DeviceMemory> visibilityBufferImageMemory_;
		std::unique_ptr<ImageView> visibilityBufferImageView_;
		
		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;
		
		std::unique_ptr<Image> motionVectorImage_;
		std::unique_ptr<DeviceMemory> motionVectorImageMemory_;
		std::unique_ptr<ImageView> motionVectorImageView_;
		
	};

}
