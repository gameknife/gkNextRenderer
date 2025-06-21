#include "PathTracingPipeline.hpp"

#include <Utilities/FileHelper.hpp>

#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorBinding.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"

namespace Vulkan::RayTracing
{
    PathTracingPipeline::PathTracingPipeline(const SwapChain& swapChain,
        const TopLevelAccelerationStructure& accelerationStructure,
        const VulkanBaseRenderer& baseRenderer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
    {
         // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
        };
        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
             // Top level acceleration structure.
            const auto accelerationStructureHandle = accelerationStructure.Handle();
            VkWriteDescriptorSetAccelerationStructureKHR structureInfo = {};
            structureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            structureInfo.pNext = nullptr;
            structureInfo.accelerationStructureCount = 1;
            structureInfo.pAccelerationStructures = &accelerationStructureHandle;
            
            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, uniformBufferInfo),
                descriptorSets.Bind(i, 1, structureInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
            descriptorSetManager_.get(),
            &baseRenderer.GetRTDescriptorSetManager(),
            &scene.GetSceneBufferDescriptorSetManager()
        };

        PipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size()),
                                                       &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/PathTracingShading.comp.slang.spv");
        
        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = PipelineLayout_->Handle();


        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create compose pipeline");
    }

    PathTracingPipeline::~PathTracingPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        PipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet PathTracingPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
