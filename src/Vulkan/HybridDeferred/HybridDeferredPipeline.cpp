#include "HybridDeferredPipeline.hpp"
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
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"

namespace Vulkan::HybridDeferred
{
    HybridShadingPipeline::HybridShadingPipeline(const SwapChain& swapChain, const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
                                                const ImageView& miniGBuffer0ImageView,
                                                const ImageView& miniGBuffer1ImageView,
                                                 const ImageView& finalImageView, const ImageView& motionVectorImageView,
                                                 const ImageView& directLight0ImageView, const ImageView& directLight1ImageView,
                                                 const ImageView& albedoImageView, const ImageView& normalImageView,
                                                 const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // MiniGbuffer and output
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            // Others like in frag
            {3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            // all buffer here
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            {9, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {10, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},

            {11, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {12, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {13, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {14, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
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

            VkDescriptorImageInfo Info0 = {NULL, miniGBuffer0ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL, miniGBuffer1ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info2 = {NULL, finalImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info8 = {NULL, motionVectorImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};

            VkDescriptorImageInfo Info11 = {NULL, directLight0ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info12 = {NULL, directLight1ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info13 = {NULL, albedoImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info14 = {NULL, normalImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
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
            
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
                descriptorSets.Bind(i, 2, Info2),
                descriptorSets.Bind(i, 3, uniformBufferInfo),
                descriptorSets.Bind(i, 4, vertexBufferInfo),
                descriptorSets.Bind(i, 5, indexBufferInfo),
                descriptorSets.Bind(i, 6, materialBufferInfo),
                descriptorSets.Bind(i, 7, offsetsBufferInfo),
                descriptorSets.Bind(i, 8, nodesBufferInfo),
                descriptorSets.Bind(i, 9, Info8),
                descriptorSets.Bind(i, 10, structureInfo),
                descriptorSets.Bind(i, 11, Info11),
                descriptorSets.Bind(i, 12, Info12),
                descriptorSets.Bind(i, 13, Info13),
                descriptorSets.Bind(i, 14, Info14)
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, "assets/shaders/HybridDeferredShading.comp.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create hybird shading pipeline");
    }

    HybridShadingPipeline::~HybridShadingPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet HybridShadingPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
