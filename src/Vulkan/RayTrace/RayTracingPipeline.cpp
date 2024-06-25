#include "RayTracingPipeline.hpp"

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
    RayTracingPipeline::RayTracingPipeline(
        const DeviceProcedures& deviceProcedures,
        const SwapChain& swapChain,
        const TopLevelAccelerationStructure& accelerationStructure,
        const ImageView& accumulationImageView,
        const ImageView& motionVectorImageView,
        const ImageView& visibilityBufferImageView,
        const ImageView& visibility1BufferImageView,
        const ImageView& OutAlbedoImageView,
        const ImageView& OutNormalImageView,
        const ImageView& AdaptiveSampleImageView,
        const std::vector<Assets::UniformBuffer>& uniformBuffers,
        const Assets::Scene& scene) :
        swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // Top level acceleration structure.
            {0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
            // Light buffer
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
            // Camera information & co
            {
                3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
            },

            // Vertex buffer, Index buffer, Material buffer, Offset buffer
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},

            // Textures and image samplers
            {
                8, static_cast<uint32_t>(scene.TextureSamplers().size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR
            },

            // The Procedural buffer.
            {
                9, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
            },

            // Images, 6 image, output, motion, gbuffer, albedo, visibility, visibility1
            {10, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
            {11, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
            {12, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
            {13, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},

            {14, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
            {15, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},

            {16, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
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

            // Light buffer
            VkDescriptorBufferInfo lightBufferInfo = {};
            lightBufferInfo.buffer = scene.LightBuffer().Handle();
            lightBufferInfo.range = VK_WHOLE_SIZE;

            // Image and texture samplers.
            std::vector<VkDescriptorImageInfo> imageInfos(scene.TextureSamplers().size());

            for (size_t t = 0; t != imageInfos.size(); ++t)
            {
                auto& imageInfo = imageInfos[t];
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = scene.TextureImageViews()[t];
                imageInfo.sampler = scene.TextureSamplers()[t];
            }

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, structureInfo),
                descriptorSets.Bind(i, 1, lightBufferInfo),
                descriptorSets.Bind(i, 3, uniformBufferInfo),
                descriptorSets.Bind(i, 4, vertexBufferInfo),
                descriptorSets.Bind(i, 5, indexBufferInfo),
                descriptorSets.Bind(i, 6, materialBufferInfo),
                descriptorSets.Bind(i, 7, offsetsBufferInfo),
                descriptorSets.Bind(i, 8, *imageInfos.data(), static_cast<uint32_t>(imageInfos.size())),
                descriptorSets.Bind(i, 10, accumulationImageInfo),
                descriptorSets.Bind(i, 11, motionVectorImageInfo),
                descriptorSets.Bind(i, 12, visibilityBufferImageInfo),
                descriptorSets.Bind(i, 13, visibility1BufferImageInfo),
                descriptorSets.Bind(i, 14, outAlbedoImageInfo),
                descriptorSets.Bind(i, 15, outNormalImageInfo),
                descriptorSets.Bind(i, 16, adaptiveSampleImageInfo),
            };

            // Procedural buffer (optional)
            VkDescriptorBufferInfo proceduralBufferInfo = {};

            if (scene.HasProcedurals())
            {
                proceduralBufferInfo.buffer = scene.ProceduralBuffer().Handle();
                proceduralBufferInfo.range = VK_WHOLE_SIZE;

                descriptorWrites.push_back(descriptorSets.Bind(i, 9, proceduralBufferInfo));
            }

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        rayTracePipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));

        // Load shaders.
        const ShaderModule rayGenShader(device, Utilities::FileHelper::GetPlatformFilePath("/assets/shaders/RayTracing.rgen.spv"));
        const ShaderModule missShader(device, Utilities::FileHelper::GetPlatformFilePath("/assets/shaders/RayTracing.rmiss.spv"));
        const ShaderModule closestHitShader(device, Utilities::FileHelper::GetPlatformFilePath("/assets/shaders/RayTracing.rchit.spv"));
        const ShaderModule proceduralClosestHitShader(device, Utilities::FileHelper::GetPlatformFilePath("/assets/shaders/RayTracing.Procedural.rchit.spv"));
        const ShaderModule proceduralIntersectionShader(device, Utilities::FileHelper::GetPlatformFilePath("/assets/shaders/RayTracing.Procedural.rint.spv"));

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages =
        {
            rayGenShader.CreateShaderStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR),
            missShader.CreateShaderStage(VK_SHADER_STAGE_MISS_BIT_KHR),
            closestHitShader.CreateShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
            proceduralClosestHitShader.CreateShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
            proceduralIntersectionShader.CreateShaderStage(VK_SHADER_STAGE_INTERSECTION_BIT_KHR),
        };

        // Shader groups
        VkRayTracingShaderGroupCreateInfoKHR rayGenGroupInfo = {};
        rayGenGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayGenGroupInfo.pNext = nullptr;
        rayGenGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayGenGroupInfo.generalShader = 0;
        rayGenGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        rayGenIndex_ = 0;

        VkRayTracingShaderGroupCreateInfoKHR missGroupInfo = {};
        missGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        missGroupInfo.pNext = nullptr;
        missGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        missGroupInfo.generalShader = 1;
        missGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        missGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        missGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        missIndex_ = 1;

        VkRayTracingShaderGroupCreateInfoKHR triangleHitGroupInfo = {};
        triangleHitGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        triangleHitGroupInfo.pNext = nullptr;
        triangleHitGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        triangleHitGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
        triangleHitGroupInfo.closestHitShader = 2;
        triangleHitGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        triangleHitGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        triangleHitGroupIndex_ = 2;

        VkRayTracingShaderGroupCreateInfoKHR proceduralHitGroupInfo = {};
        proceduralHitGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        proceduralHitGroupInfo.pNext = nullptr;
        proceduralHitGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        proceduralHitGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
        proceduralHitGroupInfo.closestHitShader = 3;
        proceduralHitGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        proceduralHitGroupInfo.intersectionShader = 4;
        proceduralHitGroupIndex_ = 3;

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups =
        {
            rayGenGroupInfo,
            missGroupInfo,
            triangleHitGroupInfo,
            proceduralHitGroupInfo,
        };

        // Create graphic pipeline
        VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.pNext = nullptr;
        pipelineInfo.flags = 0;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = 1;
        pipelineInfo.layout = rayTracePipelineLayout_->Handle();
        pipelineInfo.basePipelineHandle = nullptr;
        pipelineInfo.basePipelineIndex = 0;

        Check(deviceProcedures.vkCreateRayTracingPipelinesKHR(device.Handle(), nullptr, nullptr, 1, &pipelineInfo,
                                                              nullptr, &pipeline_),
              "create ray tracing pipeline");
    }

    RayTracingPipeline::~RayTracingPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        rayTracePipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet RayTracingPipeline::DescriptorSet(const uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    DenoiserPipeline::DenoiserPipeline(const DeviceProcedures& deviceProcedures, const SwapChain& swapChain,
                                       const TopLevelAccelerationStructure& accelerationStructure,
                                       const ImageView& pingpongImage0View,
                                       const ImageView& pingpongImage1View, const ImageView& gbufferImageView,
                                       const ImageView& albedoImageView,
                                       const std::vector<Assets::UniformBuffer>& uniformBuffers,
                                       const Assets::Scene& scene) : swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // Image accumulation & output & GBuffer.
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            // Camera information & co
            {4, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            // Accumulation image
            VkDescriptorImageInfo accumulationImageInfo = {};
            accumulationImageInfo.imageView = pingpongImage0View.Handle();
            accumulationImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Output image
            VkDescriptorImageInfo outputImageInfo = {};
            outputImageInfo.imageView = pingpongImage1View.Handle();
            outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Gbuffer image
            VkDescriptorImageInfo gbufferImageInfo = {};
            gbufferImageInfo.imageView = gbufferImageView.Handle();
            gbufferImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Albedo image
            VkDescriptorImageInfo albedoImageInfo = {};
            albedoImageInfo.imageView = albedoImageView.Handle();
            albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, accumulationImageInfo),
                descriptorSets.Bind(i, 1, outputImageInfo),
                descriptorSets.Bind(i, 2, gbufferImageInfo),
                descriptorSets.Bind(i, 3, albedoImageInfo),
                descriptorSets.Bind(i, 4, uniformBufferInfo),
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
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/Denoise.comp.spv"));

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = PipelineLayout_->Handle();


        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create denoise pipeline");
    }

    DenoiserPipeline::~DenoiserPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        PipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet DenoiserPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    ComposePipeline::ComposePipeline(const DeviceProcedures& deviceProcedures, const SwapChain& swapChain,
                                     const ImageView& final0ImageView, const ImageView& final1ImageView,
                                     const ImageView& albedoImageView, const ImageView& outImageView,
                                     const ImageView& motionVectorView,
                                     const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            // Image accumulation & output & GBuffer.
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            // Camera information & co
            {4, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL, final0ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info1 = {NULL, final1ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info2 = {NULL, albedoImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info3 = {NULL, outImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo Info4 = {uniformBuffers[i].Buffer().Handle(), 0, VK_WHOLE_SIZE};
            VkDescriptorImageInfo Info5 = {NULL, motionVectorView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
                descriptorSets.Bind(i, 2, Info2),
                descriptorSets.Bind(i, 3, Info3),
                descriptorSets.Bind(i, 4, Info4),
                descriptorSets.Bind(i, 5, Info5),
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
        const ShaderModule denoiseShader(device, Utilities::FileHelper::GetPlatformFilePath("assets/shaders/Compose.comp.spv"));

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = PipelineLayout_->Handle();


        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create compose pipeline");
    }

    ComposePipeline::~ComposePipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        PipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet ComposePipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
