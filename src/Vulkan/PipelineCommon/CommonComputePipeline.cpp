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
#include "Utilities/FileHelper.hpp"

namespace Vulkan::PipelineCommon
{
    AccumulatePipeline::AccumulatePipeline(const SwapChain& swapChain, const ImageView& sourceImageView,
                                           const ImageView& accumulateImageView, const ImageView& accumulateImage1View,
                                           const ImageView& motionVectorImageView,
                                           const ImageView& visibilityBufferImageView,
                                           const ImageView& prevVisibilityBufferImageView,
                                           const ImageView& validateImage1View,
                                           const ImageView& adaptiveSampleImageView,
                                           const std::vector<Assets::UniformBuffer>& uniformBuffers,
                                           const Assets::Scene& scene): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {4, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL, sourceImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL, accumulateImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info2 = {NULL, accumulateImage1View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info3 = {NULL, motionVectorImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info5 = {NULL, visibilityBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info6 = {NULL, prevVisibilityBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info7 = {NULL, validateImage1View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info8 = {NULL, adaptiveSampleImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
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
                descriptorSets.Bind(i, 5, Info5),
                descriptorSets.Bind(i, 6, Info6),
                descriptorSets.Bind(i, 7, Info7),
                descriptorSets.Bind(i, 8, Info8),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/Accumulate.comp.spv"));

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

    FinalComposePipeline::FinalComposePipeline(const SwapChain& swapChain,
                                               const ImageView& sourceImageView, const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, swapChain.ImageViews().size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL, sourceImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL, swapChain.ImageViews()[i]->Handle(), VK_IMAGE_LAYOUT_GENERAL};
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
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/FinalCompose.comp.spv"));

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
    }

    FinalComposePipeline::~FinalComposePipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet FinalComposePipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    VisualDebuggerPipeline::VisualDebuggerPipeline(const SwapChain& swapChain, const ImageView& debugImage1View, const ImageView& debugImage2View,
                                                   const ImageView& debugImage3View, const ImageView& debugImage4View, const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, swapChain.ImageViews().size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            
            VkDescriptorImageInfo Info0 = {NULL, swapChain.ImageViews()[i]->Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL, debugImage1View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info2 = {NULL, debugImage2View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info3 = {NULL, debugImage3View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info4 = {NULL, debugImage4View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
                descriptorSets.Bind(i, 2, Info2),
                descriptorSets.Bind(i, 3, Info3),
                descriptorSets.Bind(i, 4, Info4),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/VisualDebugger.comp.spv"));

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
    }

    VisualDebuggerPipeline::~VisualDebuggerPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet VisualDebuggerPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
