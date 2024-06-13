#pragma once

#include "Vulkan/VulkanBaseRenderer.hpp"
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

		std::unique_ptr<Image> accumulationImage_;
		std::unique_ptr<DeviceMemory> accumulationImageMemory_;
		std::unique_ptr<ImageView> accumulationImageView_;

		std::unique_ptr<Image> outputImage_;
		std::unique_ptr<DeviceMemory> outputImageMemory_;
		std::unique_ptr<ImageView> outputImageView_;

		std::unique_ptr<Image> pingpongImage0_;
		std::unique_ptr<DeviceMemory> pingpongImage0Memory_;
		std::unique_ptr<ImageView> pingpongImage0View_;

		std::unique_ptr<Image> pingpongImage1_;
		std::unique_ptr<DeviceMemory> pingpongImage1Memory_;
		std::unique_ptr<ImageView> pingpongImage1View_;
		
		std::unique_ptr<Image> gbufferImage_;
		std::unique_ptr<DeviceMemory> gbufferImageMemory_;
		std::unique_ptr<ImageView> gbufferImageView_;

		std::unique_ptr<Image> albedoImage_;
		std::unique_ptr<DeviceMemory> albedoImageMemory_;
		std::unique_ptr<ImageView> albedoImageView_;

		std::unique_ptr<Image> motionVectorImage_;
		std::unique_ptr<DeviceMemory> motionVectorImageMemory_;
		std::unique_ptr<ImageView> motionVectorImageView_;

		std::unique_ptr<Image> visibilityBufferImage_;
		std::unique_ptr<DeviceMemory> visibilityBufferImageMemory_;
		std::unique_ptr<ImageView> visibilityBufferImageView_;

		std::unique_ptr<Image> visibility1BufferImage_;
		std::unique_ptr<DeviceMemory> visibility1BufferImageMemory_;
		std::unique_ptr<ImageView> visibility1BufferImageView_;

		std::unique_ptr<Image> validateImage_;
		std::unique_ptr<DeviceMemory> validateImageMemory_;
		std::unique_ptr<ImageView> validateImageView_;
		
		std::unique_ptr<class RayTracingPipeline> rayTracingPipeline_;
		std::unique_ptr<class PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<class DenoiserPipeline> denoiserPipeline_;
		std::unique_ptr<class ComposePipeline> composePipeline_;
		std::unique_ptr<class ShaderBindingTable> shaderBindingTable_;

		
	};

}
