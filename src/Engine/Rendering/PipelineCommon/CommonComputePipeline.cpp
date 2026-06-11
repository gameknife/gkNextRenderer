#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"

#include "Engine/Runtime/Engine.hpp"

#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Data/Vertex.hpp"

namespace Vulkan::PipelineCommon
{
	namespace
	{
		// Shared tail of every zero-bind compute pipeline constructor
		VkPipeline CreateComputePipeline(const Device& device, const char* shaderfile, VkPipelineLayout layout)
		{
			const ShaderModule computeShader(device, shaderfile);

			VkComputePipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.stage = computeShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
			pipelineCreateInfo.layout = layout;

			VkPipeline pipeline = VK_NULL_HANDLE;
			Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE, 1,
				&pipelineCreateInfo, NULL, &pipeline), shaderfile);
			return pipeline;
		}

		// Shared bind + push-constant sequence of every zero-bind compute pipeline
		void BindComputeWithPush(VkCommandBuffer commandBuffer, VkPipeline pipeline,
			const class PipelineLayout& layout, uint32_t pushSize, const void* pushData)
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			layout.BindDescriptorSets(commandBuffer, 0);
			vkCmdPushConstants(commandBuffer, layout.Handle(), VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
		}
	}

	ZeroBindWithTLASPipeline::ZeroBindWithTLASPipeline(
	const SwapChain& swapChain,
	const char* shaderfile,
	const Assets::Scene& scene):PipelineBase(swapChain)
	{
		// Create descriptor pool/sets.
		const auto& device = swapChain.Device();

		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(Assets::GPUScene);

#if ANDROID
		std::vector<DescriptorBinding> descriptorBindings =
		{
			{0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
		};

		descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));
		auto& descriptorSets = descriptorSetManager_->DescriptorSets();

		const auto accelerationStructureHandle = NextEngine::GetInstance()->TryGetGPUAccelerationStructureHandle();
		VkWriteDescriptorSetAccelerationStructureKHR structureInfo = {};
		structureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		structureInfo.pNext = nullptr;
		structureInfo.accelerationStructureCount = 1;
		structureInfo.pAccelerationStructures = &accelerationStructureHandle;

		const std::vector<VkWriteDescriptorSet> descriptorWrites =
		{
			descriptorSets.Bind(0, 0, structureInfo),
		};
		descriptorSets.UpdateDescriptors(0, descriptorWrites);
#endif

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
#if ANDROID
			descriptorSetManager_.get()
#endif
		};

		pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
		pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
	}

	void ZeroBindWithTLASPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene,
		uint32_t imageIndex)
	{
		BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene),
		                    &(scene.FetchGPUScene(imageIndex)));
	}

	void ZeroBindWithTLASPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
	{
		BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene), &gpuScene);
	}

	ZeroBindPipeline::ZeroBindPipeline(
	const SwapChain& swapChain,
	const char* shaderfile,
	const Assets::Scene& scene):PipelineBase(swapChain)
	{
		// Create descriptor pool/sets.
		const auto& device = swapChain.Device();

		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(Assets::GPUScene);

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
		};

		pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
		pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
	}

	void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex)
	{
		BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene),
		                    &(scene.FetchGPUScene(imageIndex)));
	}

	void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
	{
		BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene), &gpuScene);
	}

	ZeroBindCustomPushConstantPipeline::ZeroBindCustomPushConstantPipeline(const SwapChain& swapChain,
	const char* shaderfile, uint32_t pushConstantSize):PipelineBase(swapChain),pushConstantSize_(pushConstantSize)
	{
		// Create descriptor pool/sets.
		const auto& device = swapChain.Device();

		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = pushConstantSize_;

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager()
		};

		pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
		pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
	}

	void ZeroBindCustomPushConstantPipeline::BindPipeline(VkCommandBuffer commandBuffer, const void* data)
	{
		BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), pushConstantSize_, data);
	}

    VisibilityPipeline::VisibilityPipeline(
        const SwapChain& swapChain,
        const DepthBuffer& depthBuffer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers,
        const Assets::Scene& scene) :
        PipelineBase(swapChain)
    {
        const auto& device = swapChain.Device();

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
		};

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        // Create pipeline layout and render pass.
        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        renderPass_.reset(new class RenderPass(swapChain, VK_FORMAT_R32_UINT, depthBuffer, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR));
        renderPass_->SetDebugName("Visibility Render Pass");

        const ShaderModule vertShader(device, "assets/shaders/Rast.VisibilityPassSoftMeshShader.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.VisibilityPass.frag.slang.spv");

        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertShader, fragShader)
            .SetFixedViewport({0, 0}, swapChain.RenderExtent())
            .SetDepth(true, true, VK_COMPARE_OP_LESS)
            .Build(pipelineLayout_->Handle(), renderPass_->Handle(), "create graphics pipeline");
    }

    VisibilityPipeline::~VisibilityPipeline()
    {
        renderPass_.reset();
    }

    GraphicsPipeline::GraphicsPipeline(
	const SwapChain& swapChain,
	const DepthBuffer& depthBuffer,
	const std::vector<Assets::UniformBuffer>& uniformBuffers,
	const Assets::Scene& scene,
	const bool isWireFrame) :
	PipelineBase(swapChain)
	{
        (void)uniformBuffers;
        (void)scene;

		const auto& device = swapChain.Device();

		const VkOffset2D viewportOffset = isWireFrame ? swapChain.OutputOffset() : swapChain.RenderOffset();
		const VkExtent2D viewportExtent = isWireFrame ? swapChain.OutputExtent() : swapChain.RenderExtent();

		VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
		vkGetPhysicalDeviceFeatures(device.PhysicalDevice(), &physicalDeviceFeatures);

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
		};

		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(Assets::GPUScene);

		// Create pipeline layout and render pass.
		pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
		renderPass_.reset(new class RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD));
		renderPass_->SetDebugName("Wireframe Render Pass");

		const ShaderModule vertShader(device, "assets/shaders/Rast.WireframeSoftMeshShader.vert.slang.spv");
		const ShaderModule fragShader(device, "assets/shaders/Rast.Wireframe.frag.slang.spv");

		pipeline_ = GraphicsPipelineBuilder(device)
			.SetShaders(vertShader, fragShader)
			.SetFixedViewport(viewportOffset, viewportExtent)
			.SetPolygonMode(isWireFrame && physicalDeviceFeatures.fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)
			.SetDepth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
			.SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)
			.Build(pipelineLayout_->Handle(), renderPass_->Handle(), "create graphics pipeline");
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		renderPass_.reset();
	}
}
