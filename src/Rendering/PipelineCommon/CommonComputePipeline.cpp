#include "CommonComputePipeline.hpp"

#include "Runtime/Engine.hpp"

#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/RenderPass.hpp"

#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Data/Vertex.hpp"

namespace Vulkan::PipelineCommon
{
	ZeroBindWithTLASPipeline::ZeroBindWithTLASPipeline(const SwapChain& swapChain, const char* shaderfile):PipelineBase(swapChain)
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
		
		const ShaderModule denoiseShader(device, shaderfile);
        
		VkComputePipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
		pipelineCreateInfo.layout = pipelineLayout_->Handle();

		Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,1,
			&pipelineCreateInfo,NULL, &pipeline_),shaderfile);
	}

	void ZeroBindWithTLASPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene,
		uint32_t imageIndex)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Handle());
		PipelineLayout().BindDescriptorSets(commandBuffer, 0);
		vkCmdPushConstants(commandBuffer, PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
						   0, sizeof(Assets::GPUScene), &(scene.FetchGPUScene(imageIndex)));
	}
    
	ZeroBindPipeline::ZeroBindPipeline(const SwapChain& swapChain, const char* shaderfile):PipelineBase(swapChain)
	{
		// Create descriptor pool/sets.
		const auto& device = swapChain.Device();
        
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(Assets::GPUScene);

		std::vector<DescriptorSetManager*> managers = {
			&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager()
		};

		pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
		
		const ShaderModule denoiseShader(device, shaderfile);
        
		VkComputePipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
		pipelineCreateInfo.layout = pipelineLayout_->Handle();

		Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,1,
			&pipelineCreateInfo,NULL, &pipeline_),shaderfile);
	}

	void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Handle());
		PipelineLayout().BindDescriptorSets(commandBuffer, 0);
		vkCmdPushConstants(commandBuffer, PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
						   0, sizeof(Assets::GPUScene), &(scene.FetchGPUScene(imageIndex)));
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
		
		const ShaderModule denoiseShader(device, shaderfile);
        
		VkComputePipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
		pipelineCreateInfo.layout = pipelineLayout_->Handle();

		Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,1,
			&pipelineCreateInfo,NULL, &pipeline_),shaderfile);
	}
	
	void ZeroBindCustomPushConstantPipeline::BindPipeline(VkCommandBuffer commandBuffer, const void* data)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Handle());
		PipelineLayout().BindDescriptorSets(commandBuffer, 0);
		vkCmdPushConstants(commandBuffer, PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,0, pushConstantSize_, data);
	}
	
    VisibilityPipeline::VisibilityPipeline(
        const SwapChain& swapChain,
        const DepthBuffer& depthBuffer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers,
        const Assets::Scene& scene) :
        PipelineBase(swapChain)
    {
        const auto& device = swapChain.Device();
        //const auto bindingDescription = Assets::GPUVertex::GetBindingDescription();
        //const auto attributeDescriptions = Assets::GPUVertex::GetFastAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        //vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        //vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        //vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.RenderExtent().width);
        viewport.height = static_cast<float>(swapChain.RenderExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = swapChain.RenderExtent();

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {}; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional
    	
    	VkPushConstantRange pushConstantRange{};
    	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    	pushConstantRange.offset = 0;
    	pushConstantRange.size = sizeof(Assets::GPUScene);

        // Create pipeline layout and render pass.
        pipelineLayout_.reset(new class PipelineLayout(device, &pushConstantRange, 1));
        renderPass_.reset(new class RenderPass(swapChain, VK_FORMAT_R32_UINT, depthBuffer, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR));
        renderPass_->SetDebugName("Visibility Render Pass");
        // Load shaders.
        const ShaderModule vertShader(device, "assets/shaders/Rast.VisibilityPass.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.VisibilityPass.frag.slang.spv");

        VkPipelineShaderStageCreateInfo shaderStages[] =
        {
            vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
            fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        // Create graphic pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr; // Optional
        pipelineInfo.basePipelineHandle = nullptr; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional
        pipelineInfo.layout = pipelineLayout_->Handle();
        pipelineInfo.renderPass = renderPass_->Handle();
        pipelineInfo.subpass = 0;

        Check(vkCreateGraphicsPipelines(device.Handle(), nullptr, 1, &pipelineInfo, nullptr, &pipeline_),
              "create graphics pipeline");
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
		const auto& device = swapChain.Device();
		const auto bindingDescription = Assets::GPUVertex::GetFastBindingDescription();
		const auto attributeDescriptions = Assets::GPUVertex::GetFastAttributeDescriptions();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChain.RenderExtent().width);
		viewport.height = static_cast<float>(swapChain.RenderExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChain.RenderExtent();

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = isWireFrame ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.minDepthBounds = 0.0f; // Optional
		depthStencil.maxDepthBounds = 1.0f; // Optional
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = {}; // Optional
		depthStencil.back = {}; // Optional

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // Optional
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		// Create descriptor pool/sets.
		std::vector<DescriptorBinding> descriptorBindings =
		{
			{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
		};

		descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

		auto& descriptorSets = descriptorSetManager_->DescriptorSets();

		for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
		{
			// Uniform buffer
			VkDescriptorBufferInfo uniformBufferInfo = {};
			uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
			uniformBufferInfo.range = VK_WHOLE_SIZE;

			// Nodes buffer
			VkDescriptorBufferInfo nodesBufferInfo = {};
			nodesBufferInfo.buffer = scene.NodeMatrixBuffer().Handle();
			nodesBufferInfo.range = VK_WHOLE_SIZE;

			const std::vector<VkWriteDescriptorSet> descriptorWrites =
			{
				descriptorSets.Bind(i, 0, uniformBufferInfo),
				descriptorSets.Bind(i, 1, nodesBufferInfo),
			};

			descriptorSets.UpdateDescriptors(i, descriptorWrites);
		}

		VkPushConstantRange pushConstantRange{};
		// Push constants will only be accessible at the selected pipeline stages, for this sample it's the vertex shader that reads them
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = 4 * 16;
		
		// Create pipeline layout and render pass.
		pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
		renderPass_.reset(new class RenderPass(swapChain, VK_FORMAT_R16G16B16A16_SFLOAT, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD));
		renderPass_->SetDebugName("Wireframe Render Pass");

		// Load shaders.
		const ShaderModule vertShader(device, "assets/shaders/Rast.Wireframe.vert.slang.spv");
		const ShaderModule fragShader(device, "assets/shaders/Rast.Wireframe.frag.slang.spv");

		VkPipelineShaderStageCreateInfo shaderStages[] =
		{
			vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
			fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		// Create graphic pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = nullptr; // Optional
		pipelineInfo.basePipelineHandle = nullptr; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional
		pipelineInfo.layout = pipelineLayout_->Handle();
		pipelineInfo.renderPass = renderPass_->Handle();
		pipelineInfo.subpass = 0;

		Check(vkCreateGraphicsPipelines(device.Handle(), nullptr, 1, &pipelineInfo, nullptr, &pipeline_),
			"create graphics pipeline");
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		renderPass_.reset();
	}
}
