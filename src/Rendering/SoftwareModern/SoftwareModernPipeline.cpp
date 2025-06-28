#include "SoftwareModernPipeline.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSets.hpp"
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
#include "Rendering/VulkanBaseRenderer.hpp"

namespace Vulkan::LegacyDeferred {

ShadingPipeline::ShadingPipeline(const SwapChain& swapChain,
	const VulkanBaseRenderer& baseRenderer,
	const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
{
	// Create descriptor pool/sets.
    const auto& device = swapChain.Device();
    const std::vector<DescriptorBinding> descriptorBindings =
    {
		{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
		{1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
    };

    descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

    auto& descriptorSets = descriptorSetManager_->DescriptorSets();

    for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
    {
        // Uniform buffer
        VkDescriptorBufferInfo uniformBufferInfo = {};
        uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        uniformBufferInfo.range = VK_WHOLE_SIZE;
        
        VkDescriptorImageInfo Info12 = {scene.ShadowMap().Sampler().Handle(), scene.ShadowMap().ImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL};
        
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(i, 0, uniformBufferInfo),
			descriptorSets.Bind(i, 1, Info12),
        };

        descriptorSets.UpdateDescriptors(i, descriptorWrites);
    }

	std::vector<DescriptorSetManager*> managers = {
        	&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
			descriptorSetManager_.get(),
			&baseRenderer.GetRTDescriptorSetManager(),
			&scene.GetSceneBufferDescriptorSetManager()
		};
	pipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size())));
    const ShaderModule denoiseShader(device, "assets/shaders/Core.SwModern.comp.slang.spv");

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
	ShadingPipeline::ShadingPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRenderer,
		const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
	{
		 // Create descriptor pool/sets.
	        const auto& device = swapChain.Device();
	        const std::vector<DescriptorBinding> descriptorBindings =
	        {
				{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
	        };

	        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

	        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

	        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
	        {
        		// Uniform buffer
        		VkDescriptorBufferInfo uniformBufferInfo = {};
        		uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        		uniformBufferInfo.range = VK_WHOLE_SIZE;
	        	
	            std::vector<VkWriteDescriptorSet> descriptorWrites =
	            {
	                descriptorSets.Bind(i, 0, uniformBufferInfo),
	            };
	            descriptorSets.UpdateDescriptors(i, descriptorWrites);
	        }

			std::vector<DescriptorSetManager*> managers = {
	        		&Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
					descriptorSetManager_.get(),
					&baseRenderer.GetRTDescriptorSetManager(),
					&scene.GetSceneBufferDescriptorSetManager()
				};
			pipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size())));
	        const ShaderModule denoiseShader(device, "assets/shaders/Core.VoxelTracing.comp.slang.spv");

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
