#include "LegacyDeferredPipeline.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Vertex.hpp"
#include "Utilities/FileHelper.hpp"

namespace Vulkan::LegacyDeferred {

GBufferPipeline::GBufferPipeline(
	const SwapChain& swapChain, 
	const DepthBuffer& depthBuffer,
	const std::vector<Assets::UniformBuffer>& uniformBuffers,
	const Assets::Scene& scene) :
	swapChain_(swapChain)
{
	const auto& device = swapChain.Device();
	const auto bindingDescription = Assets::Vertex::GetBindingDescription();
	const auto attributeDescriptions = Assets::Vertex::GetAttributeDescriptions();

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
	viewport.width = static_cast<float>(swapChain.Extent().width);
	viewport.height = static_cast<float>(swapChain.Extent().height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = swapChain.Extent();

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

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
	for( int i = 0; i < 3; ++i)
	{
		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		colorBlendAttachments.push_back(colorBlendAttachment);
	}


	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	colorBlending.pAttachments = colorBlendAttachments.data();
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	// Create descriptor pool/sets.
	std::vector<DescriptorBinding> descriptorBindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
		{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{3, static_cast<uint32_t>(scene.TextureSamplers().size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}
	};

	descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

	auto& descriptorSets = descriptorSetManager_->DescriptorSets();

	for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
	{
		// Uniform buffer
		VkDescriptorBufferInfo uniformBufferInfo = {};
		uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
		uniformBufferInfo.range = VK_WHOLE_SIZE;

		// Material buffer
		VkDescriptorBufferInfo materialBufferInfo = {};
		materialBufferInfo.buffer = scene.MaterialBuffer().Handle();
		materialBufferInfo.range = VK_WHOLE_SIZE;

		// Nodes buffer
		VkDescriptorBufferInfo nodesBufferInfo = {};
		nodesBufferInfo.buffer = scene.NodeMatrixBuffer().Handle();
		nodesBufferInfo.range = VK_WHOLE_SIZE;

		// Image and texture samplers
		std::vector<VkDescriptorImageInfo> imageInfos(scene.TextureSamplers().size());

		for (size_t t = 0; t != imageInfos.size(); ++t)
		{
			auto& imageInfo = imageInfos[t];
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = scene.TextureImageViews()[t];
			imageInfo.sampler = scene.TextureSamplers()[t];
		}

		const std::vector<VkWriteDescriptorSet> descriptorWrites =
		{
			descriptorSets.Bind(i, 0, uniformBufferInfo),
			descriptorSets.Bind(i, 1, materialBufferInfo),
			descriptorSets.Bind(i, 2, nodesBufferInfo),
			descriptorSets.Bind(i, 3, *imageInfos.data(), static_cast<uint32_t>(imageInfos.size()))
		};

		descriptorSets.UpdateDescriptors(i, descriptorWrites);
	}

	// Create pipeline layout and render pass.
	pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
	renderPass_.reset(new class RenderPass(swapChain, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_B8G8R8A8_UNORM, depthBuffer,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR));

	// Load shaders.
	const ShaderModule vertShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/GBufferPass.vert.spv"));
	const ShaderModule fragShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/GBufferPass.frag.spv"));

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

GBufferPipeline::~GBufferPipeline()
{
	if (pipeline_ != nullptr)
	{
		vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
		pipeline_ = nullptr;
	}
	
	renderPass_.reset();
	pipelineLayout_.reset();
	descriptorSetManager_.reset();
}

VkDescriptorSet GBufferPipeline::DescriptorSet(const uint32_t index) const
{
	return descriptorSetManager_->DescriptorSets().Handle(index);
}

ShadingPipeline::ShadingPipeline(const SwapChain& swapChain, const ImageView& gbuffer0ImageView,
	const ImageView& gbuffer1ImageView,
	const ImageView& gbuffer2ImageView,
	const ImageView& finalImageView,
	const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
{
	 // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // MiniGbuffer and output
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
			{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        		
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        	
        	// Others like in frag
			{4, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL,  gbuffer0ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	VkDescriptorImageInfo Info1 = {NULL,  gbuffer1ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	VkDescriptorImageInfo Info2 = {NULL,  gbuffer2ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info3 = {NULL,  finalImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	
        	// Uniform buffer
        	VkDescriptorBufferInfo uniformBufferInfo = {};
        	uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        	uniformBufferInfo.range = VK_WHOLE_SIZE;
        	
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
            	descriptorSets.Bind(i, 2, Info2),
            	descriptorSets.Bind(i, 3, Info3),
                descriptorSets.Bind(i, 4, uniformBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/LegacyDeferredShading.comp.spv"));

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();
	
        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
}

ShadingPipeline::~ShadingPipeline()
{
	if (pipeline_ != nullptr)
	{
		vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
		pipeline_ = nullptr;
	}

	pipelineLayout_.reset();
	descriptorSetManager_.reset();
}

VkDescriptorSet ShadingPipeline::DescriptorSet(uint32_t index) const
{
	return descriptorSetManager_->DescriptorSets().Handle(index);
}
}
