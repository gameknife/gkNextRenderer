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
	class VulkanBaseRenderer;
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
			const SwapChain& swapChain, const VulkanBaseRenderer& baseRender,
			const ImageView& accumulateImageView,
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
			const VulkanBaseRenderer& baseRender,
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

	class SimpleComposePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(SimpleComposePipeline)
	
		SimpleComposePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
		~SimpleComposePipeline();

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
			const SwapChain& swapChain, const VulkanBaseRenderer& baseRender);
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
			const VulkanBaseRenderer& baseRender,
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

	class HardwareGPULightBakePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(HardwareGPULightBakePipeline)
	
		HardwareGPULightBakePipeline(
			const SwapChain& swapChain,
			const DeviceProcedures& deviceProcedures,
			const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~HardwareGPULightBakePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const DeviceProcedures& deviceProcedures_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class SoftwareGPULightBakePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(SoftwareGPULightBakePipeline)
	
		SoftwareGPULightBakePipeline(
			const SwapChain& swapChain,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~SoftwareGPULightBakePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class GPUCullPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(GPUCullPipeline)
	
		GPUCullPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender, const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene);
		~GPUCullPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class VisibilityPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(VisibilityPipeline)

		VisibilityPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~VisibilityPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
		const Vulkan::RenderPass& RenderPass() const { return *renderPass_; }

	private:
		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
		std::unique_ptr<Vulkan::RenderPass> renderPass_;
		std::unique_ptr<Vulkan::RenderPass> swapRenderPass_;
	};

	class GraphicsPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(GraphicsPipeline)

		GraphicsPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene,
			bool isWireFrame);
		~GraphicsPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		bool IsWireFrame() const { return isWireFrame_; }
		const class PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
		const class RenderPass& RenderPass() const { return *renderPass_; }

	private:

		const SwapChain& swapChain_;
		const bool isWireFrame_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<class DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> pipelineLayout_;
		std::unique_ptr<class RenderPass> renderPass_;
	};
}
