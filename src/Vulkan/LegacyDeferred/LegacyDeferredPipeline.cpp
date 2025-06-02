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
#include "Assets/TextureImage.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Vertex.hpp"
#include "Utilities/FileHelper.hpp"

namespace Vulkan::LegacyDeferred {

ShadingPipeline::ShadingPipeline(const SwapChain& swapChain,
	const ImageView& visibiliyBufferImageView,
	const ImageView& finalImageView,
	const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
{
	 // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
	        {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
			{2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

        	// all buffer here
			{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

			{8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{9, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{10, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
			{11, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
        	VkDescriptorImageInfo Info0 = {NULL,  visibiliyBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL,  finalImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	
        	// Uniform buffer
        	VkDescriptorBufferInfo uniformBufferInfo = {};
        	uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        	uniformBufferInfo.range = VK_WHOLE_SIZE;

        	// Vertex buffer
        	VkDescriptorBufferInfo vertexBufferInfo = {};
        	vertexBufferInfo.buffer = scene.VertexBuffer().Handle();
        	vertexBufferInfo.range = VK_WHOLE_SIZE;

        	// Index buffer
        	VkDescriptorBufferInfo indexBufferInfo = {};
        	indexBufferInfo.buffer = scene.IndexBuffer().Handle();
        	indexBufferInfo.range = VK_WHOLE_SIZE;

        	// Material buffer
        	VkDescriptorBufferInfo materialBufferInfo = {};
        	materialBufferInfo.buffer = scene.MaterialBuffer().Handle();
        	materialBufferInfo.range = VK_WHOLE_SIZE;

        	// Offsets buffer
        	VkDescriptorBufferInfo offsetsBufferInfo = {};
        	offsetsBufferInfo.buffer = scene.OffsetsBuffer().Handle();
        	offsetsBufferInfo.range = VK_WHOLE_SIZE;

        	// Nodes buffer
        	VkDescriptorBufferInfo nodesBufferInfo = {};
        	nodesBufferInfo.buffer = scene.NodeMatrixBuffer().Handle();
        	nodesBufferInfo.range = VK_WHOLE_SIZE;
        	
        	VkDescriptorBufferInfo hdrshBufferInfo = {};
        	hdrshBufferInfo.buffer = scene.HDRSHBuffer().Handle();
        	hdrshBufferInfo.range = VK_WHOLE_SIZE;

        	VkDescriptorBufferInfo ambientCubeBufferInfo = {};
        	ambientCubeBufferInfo.buffer = scene.AmbientCubeBuffer().Handle();
        	ambientCubeBufferInfo.range = VK_WHOLE_SIZE;

        	VkDescriptorBufferInfo farAmbientCubeBufferInfo = {};
        	farAmbientCubeBufferInfo.buffer = scene.FarAmbientCubeBuffer().Handle();
        	farAmbientCubeBufferInfo.range = VK_WHOLE_SIZE;

        	VkDescriptorImageInfo Info12 = {scene.ShadowMap().Sampler().Handle(), scene.ShadowMap().ImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
            	descriptorSets.Bind(i, 0, Info0),
            	descriptorSets.Bind(i, 1, Info1),
                descriptorSets.Bind(i, 2, uniformBufferInfo),

            	descriptorSets.Bind(i, 3, vertexBufferInfo),
				descriptorSets.Bind(i, 4, indexBufferInfo),
				descriptorSets.Bind(i, 5, materialBufferInfo),
				descriptorSets.Bind(i, 6, offsetsBufferInfo),
				descriptorSets.Bind(i, 7, nodesBufferInfo),
            	
            	descriptorSets.Bind(i, 8, ambientCubeBufferInfo),
            	descriptorSets.Bind(i, 9, farAmbientCubeBufferInfo),
            	descriptorSets.Bind(i, 10, hdrshBufferInfo),
				descriptorSets.Bind(i, 11, Info12),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, "assets/shaders/LegacyDeferredShading.comp.slang.spv");

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

namespace Vulkan::VoxelTracing
{
	ShadingPipeline::ShadingPipeline(const SwapChain& swapChain, const ImageView& finalImageView,
		const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
	{
		 // Create descriptor pool/sets.
	        const auto& device = swapChain.Device();
	        const std::vector<DescriptorBinding> descriptorBindings =
	        {
	            // MiniGbuffer and output
	            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
	        	
        		// Others like in frag
				{1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

				{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
				{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
				{4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
					{5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
	        };

	        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

	        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

	        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
	        {
	            VkDescriptorImageInfo Info0 = {NULL,  finalImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        		
        		// Uniform buffer
        		VkDescriptorBufferInfo uniformBufferInfo = {};
        		uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        		uniformBufferInfo.range = VK_WHOLE_SIZE;

	        	VkDescriptorBufferInfo matBufferInfo = {};
	        	matBufferInfo.buffer = scene.MaterialBuffer().Handle();
	        	matBufferInfo.range = VK_WHOLE_SIZE;
	        	
        		VkDescriptorBufferInfo hdrshBufferInfo = {};
        		hdrshBufferInfo.buffer = scene.HDRSHBuffer().Handle();
        		hdrshBufferInfo.range = VK_WHOLE_SIZE;

	        	VkDescriptorBufferInfo ambientCubeBufferInfo = {};
	        	ambientCubeBufferInfo.buffer = scene.AmbientCubeBuffer().Handle();
	        	ambientCubeBufferInfo.range = VK_WHOLE_SIZE;

	        	VkDescriptorBufferInfo farAmbientCubeBufferInfo = {};
	        	farAmbientCubeBufferInfo.buffer = scene.FarAmbientCubeBuffer().Handle();
	        	farAmbientCubeBufferInfo.range = VK_WHOLE_SIZE;
	        	
	            std::vector<VkWriteDescriptorSet> descriptorWrites =
	            {
	                descriptorSets.Bind(i, 0, Info0),
	                descriptorSets.Bind(i, 1, uniformBufferInfo),
	            	descriptorSets.Bind(i, 2, matBufferInfo),
            		descriptorSets.Bind(i, 3, hdrshBufferInfo),
            		descriptorSets.Bind(i, 4, ambientCubeBufferInfo),
	                descriptorSets.Bind(i, 5, ambientCubeBufferInfo),
	            };

	            descriptorSets.UpdateDescriptors(i, descriptorWrites);
	        }

	        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
	        const ShaderModule denoiseShader(device, "assets/shaders/VoxelTracing.comp.slang.spv");

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
