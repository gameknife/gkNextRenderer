#pragma once

#include "Vulkan/Vulkan.hpp"
#include <memory>
#include <vector>

namespace Assets
{
	class Scene;
	class UniformBuffer;
}

namespace Vulkan
{
	class DescriptorSetManager;
	class ImageView;
	class PipelineLayout;
	class SwapChain;
	class DeviceProcedures;
}

namespace Vulkan::RayTracing
{
	
	class TopLevelAccelerationStructure;
	
	class RayQueryPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(RayQueryPipeline)

		RayQueryPipeline(
			const DeviceProcedures& deviceProcedures,
			const SwapChain& swapChain,
			const TopLevelAccelerationStructure& accelerationStructure,
			const ImageView& accumulationImageView,
			const ImageView& motionVectorImageView,
			const ImageView& visibilityBufferImageView,
			const ImageView& visibility1BufferImageView,
			const ImageView& OutAlbedoImageView,
			const ImageView& OutNormalImageView,
			const ImageView& AdaptiveSampleImageView,
			const ImageView& OutShaderTimerImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~RayQueryPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const class PipelineLayout& PipelineLayout() const { return *PipelineLayout_; }
	private:

		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> PipelineLayout_;
	};
	
}
