#include "HardwareTracingPipeline.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"

namespace Vulkan::HybridDeferred
{
    HardwareTracingPipeline::HardwareTracingPipeline(const SwapChain& swapChain, const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
                                                const VulkanBaseRenderer& baseRenderer,
                                                 const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene): swapChain_(swapChain)
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
            const auto accelerationStructureHandle = accelerationStructure.Handle();
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, {uniformBuffers[i].Buffer().Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 1, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr, 1, &accelerationStructureHandle}),
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
        
        const ShaderModule denoiseShader(device, "assets/shaders/HybridDeferredShading.comp.slang.spv");
        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create hybird shading pipeline");
    }

    HardwareTracingPipeline::~HardwareTracingPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet HardwareTracingPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
