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

	class RayTracingPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(RayTracingPipeline)

		RayTracingPipeline(
			const DeviceProcedures& deviceProcedures,
			const SwapChain& swapChain,
			const TopLevelAccelerationStructure& accelerationStructure,
			const ImageView& accumulationImageView,
			const ImageView& motionVectorImageView,
			const ImageView& visibilityBufferImageView,
			const ImageView& visibility1BufferImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~RayTracingPipeline();

		uint32_t RayGenShaderIndex() const { return rayGenIndex_; }
		uint32_t MissShaderIndex() const { return missIndex_; }
		uint32_t TriangleHitGroupIndex() const { return triangleHitGroupIndex_; }
		uint32_t ProceduralHitGroupIndex() const { return proceduralHitGroupIndex_; }

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const class PipelineLayout& PipelineLayout() const { return *rayTracePipelineLayout_; }

	private:

		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> rayTracePipelineLayout_;

		uint32_t rayGenIndex_;
		uint32_t missIndex_;
		uint32_t triangleHitGroupIndex_;
		uint32_t proceduralHitGroupIndex_;
	};
	
	class DenoiserPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(DenoiserPipeline)

		DenoiserPipeline(
			const DeviceProcedures& deviceProcedures,
			const SwapChain& swapChain,
			const TopLevelAccelerationStructure& accelerationStructure,
			const ImageView& pingpongImage0View,
			const ImageView& pingpongImage1View,
			const ImageView& gbufferImageView,
			const ImageView& albedoImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~DenoiserPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const class PipelineLayout& PipelineLayout() const { return *PipelineLayout_; }
	private:

		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> PipelineLayout_;
	};

	class ComposePipeline final
	{
	public:

		VULKAN_NON_COPIABLE(ComposePipeline)

		ComposePipeline(
			const DeviceProcedures& deviceProcedures,
			const SwapChain& swapChain,
			const ImageView& final0ImageView,
			const ImageView& final1ImageView,
			const ImageView& albedoImageView,
			const ImageView& outImageView,
			const ImageView& motionVectorView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
		~ComposePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const class PipelineLayout& PipelineLayout() const { return *PipelineLayout_; }
	private:

		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> PipelineLayout_;
	};
}
