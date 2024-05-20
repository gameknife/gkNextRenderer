#include "CommonComputePipeline.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Vertex.hpp"

namespace Vulkan::PipelineCommon {
	
AccumulatePipeline::AccumulatePipeline(const SwapChain& swapChain, const ImageView& sourceImageView, const ImageView& accumulateImageView,
	const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
{
	 // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
			{2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL,  sourceImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL,  accumulateImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
        	
        	// Uniform buffer
        	VkDescriptorBufferInfo uniformBufferInfo = {};
        	uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        	uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
				descriptorSets.Bind(i, 0, Info0),
				descriptorSets.Bind(i, 1, Info1),
				descriptorSets.Bind(i, 2, uniformBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, "../assets/shaders/Accumulate.comp.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();
	
        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
}

AccumulatePipeline::~AccumulatePipeline()
{
	if (pipeline_ != nullptr)
	{
		vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
		pipeline_ = nullptr;
	}

	pipelineLayout_.reset();
	descriptorSetManager_.reset();
}

VkDescriptorSet AccumulatePipeline::DescriptorSet(uint32_t index) const
{
	return descriptorSetManager_->DescriptorSets().Handle(index);
}
}
