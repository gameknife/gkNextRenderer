#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/ImageView.hpp"
#include <memory>
#include <vector>
#include <glm/vec4.hpp>

namespace Assets
{
	class Scene;
	class UniformBuffer;
}

namespace Vulkan
{
	class DepthBuffer;
	class PipelineLayout;
	class RenderPass;
	class SwapChain;
	class DescriptorSetManager;
	class DeviceProcedures;
	class Buffer;
}

namespace Vulkan::RayTracing
{
	class TopLevelAccelerationStructure;
}

namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(AccumulatePipeline)
	
		AccumulatePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView, const ImageView& accumulateImageView, const ImageView& prevAccumulateImageView, const ImageView& motionVectorImageView,
			const ImageView& visibilityBufferImageView,const ImageView& prevVisibilityBufferImageView,
			const ImageView& outputImage1View,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~AccumulatePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class FinalComposePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(FinalComposePipeline)
	
		FinalComposePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView,
			const ImageView& albedoBufferImageView,
			const ImageView& normalBufferImageView,
			const ImageView& visibility0ImageView,
			const ImageView& visibility1ImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
		~FinalComposePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class BufferClearPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(BufferClearPipeline)
	
		BufferClearPipeline(
			const SwapChain& swapChain);
		~BufferClearPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class VisualDebuggerPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(VisualDebuggerPipeline)
	
		VisualDebuggerPipeline(
			const SwapChain& swapChain, 
			const ImageView& debugImage1View,
			const ImageView& debugImage2View,
			const ImageView& debugImage3View,
			const ImageView& debugImage4View,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
		~VisualDebuggerPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class RayCastPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(RayCastPipeline)
	
		RayCastPipeline(
			const DeviceProcedures& deviceProcedures,
			const Buffer& ioBuffer,
			const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const Assets::Scene& scene);
		~RayCastPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const DeviceProcedures& deviceProcedures_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class AmbientGenPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(AmbientGenPipeline)
	
		AmbientGenPipeline(
			const SwapChain& swapChain,
			const DeviceProcedures& deviceProcedures,
			const Buffer& ioBuffer,
			const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~AmbientGenPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const DeviceProcedures& deviceProcedures_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};
}
