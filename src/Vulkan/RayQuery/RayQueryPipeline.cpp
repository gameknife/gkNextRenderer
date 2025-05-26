#include "RayQueryPipeline.hpp"

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

namespace Vulkan::RayTracing
{
    RayQueryPipeline::RayQueryPipeline(const DeviceProcedures& deviceProcedures, const SwapChain& swapChain,
        const TopLevelAccelerationStructure& accelerationStructure,
        const ImageView& accumulationImageView,
        const ImageView& motionVectorImageView,
        const ImageView& visibilityBufferImageView,
        const ImageView& visibility1BufferImageView,
        const ImageView& OutAlbedoImageView,
        const ImageView& OutNormalImageView,
        const ImageView& AdaptiveSampleImageView,
        const ImageView& OutShaderTimerImageView,
        
        const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
    {
         // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // Top level acceleration structure.
            {0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
            // Light buffer
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            // Camera information & co
            {3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            // Vertex buffer, Index buffer, Material buffer, Offset buffer
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {9, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            {10, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {11, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {12, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {13, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {14, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {15, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {16, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {17, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

            {18, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {19, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
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

            // Light buffer
            VkDescriptorBufferInfo lightBufferInfo = {};
            lightBufferInfo.buffer = scene.LightBuffer().Handle();
            lightBufferInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo hdrshBufferInfo = {};
            hdrshBufferInfo.buffer = scene.HDRSHBuffer().Handle();
            hdrshBufferInfo.range = VK_WHOLE_SIZE;

            // Accumulation image
            VkDescriptorImageInfo accumulationImageInfo = {};
            accumulationImageInfo.imageView = accumulationImageView.Handle();
            accumulationImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Output image
            VkDescriptorImageInfo motionVectorImageInfo = {};
            motionVectorImageInfo.imageView = motionVectorImageView.Handle();
            motionVectorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            
            VkDescriptorImageInfo visibilityBufferImageInfo = {};
            visibilityBufferImageInfo.imageView = visibilityBufferImageView.Handle();
            visibilityBufferImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo visibility1BufferImageInfo = {};
            visibility1BufferImageInfo.imageView = visibility1BufferImageView.Handle();
            visibility1BufferImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo outAlbedoImageInfo = {};
            outAlbedoImageInfo.imageView = OutAlbedoImageView.Handle();
            outAlbedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo outNormalImageInfo = {};
            outNormalImageInfo.imageView = OutNormalImageView.Handle();
            outNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo adaptiveSampleImageInfo = {};
            adaptiveSampleImageInfo.imageView = AdaptiveSampleImageView.Handle();
            adaptiveSampleImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo outShaderTimerImageInfo = {};
            outShaderTimerImageInfo.imageView = OutShaderTimerImageView.Handle();
            outShaderTimerImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorBufferInfo cubeBufferInfo = {};
            cubeBufferInfo.buffer = scene.AmbientCubeBuffer().Handle();
            cubeBufferInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo farcubeBufferInfo = {};
            farcubeBufferInfo.buffer = scene.FarAmbientCubeBuffer().Handle();
            farcubeBufferInfo.range = VK_WHOLE_SIZE;
            
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, structureInfo),
                descriptorSets.Bind(i, 1, lightBufferInfo),
                descriptorSets.Bind(i, 3, uniformBufferInfo),
                descriptorSets.Bind(i, 4, vertexBufferInfo),
                descriptorSets.Bind(i, 5, indexBufferInfo),
                descriptorSets.Bind(i, 6, materialBufferInfo),
                descriptorSets.Bind(i, 7, offsetsBufferInfo),
                descriptorSets.Bind(i, 8, nodesBufferInfo),
                descriptorSets.Bind(i, 9, hdrshBufferInfo),
                descriptorSets.Bind(i, 10, accumulationImageInfo),
                descriptorSets.Bind(i, 11, motionVectorImageInfo),
                descriptorSets.Bind(i, 12, visibilityBufferImageInfo),
                descriptorSets.Bind(i, 13, visibility1BufferImageInfo),
                descriptorSets.Bind(i, 14, outAlbedoImageInfo),
                descriptorSets.Bind(i, 15, outNormalImageInfo),
                descriptorSets.Bind(i, 16, adaptiveSampleImageInfo),
                descriptorSets.Bind(i, 17, outShaderTimerImageInfo),
                descriptorSets.Bind(i, 18, cubeBufferInfo),
                descriptorSets.Bind(i, 19, farcubeBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        // Push constants will only be accessible at the selected pipeline stages, for this sample it's the vertex shader that reads them
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;

        PipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(),
                                                       &pushConstantRange, 1));
        //const ShaderModule denoiseShader(device, "assets/shaders/RayQuery.comp.spv");
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

    RayQueryPipeline::~RayQueryPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        PipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet RayQueryPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
