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
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"

namespace Vulkan::PipelineCommon
{
    AccumulatePipeline::AccumulatePipeline(const SwapChain& swapChain, const ImageView& sourceImageView,
                                           const ImageView& accumulateImageView,
                                           const ImageView& motionVectorImageView,
                                           const ImageView& visibilityBufferImageView,
                                           const ImageView& prevVisibilityBufferImageView,
                                           const ImageView& outputImage1View, const ImageView& normalImage1View,
                                           const std::vector<Assets::UniformBuffer>& uniformBuffers,
                                           const Assets::Scene& scene): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},

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

            VkDescriptorImageInfo Info3 = {NULL, motionVectorImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info5 = {NULL, visibilityBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info6 = {NULL, prevVisibilityBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info7 = {NULL, outputImage1View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info8 = {NULL, normalImage1View.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            
            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
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
        const ShaderModule denoiseShader(device, "assets/shaders/Accumulate.comp.slang.spv");

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
                                               const ImageView& sourceImageView,
                                               const ImageView& albedoBufferImageView,
                                               const ImageView& normalBufferImageView,
                                                const ImageView& visibility0ImageView,
                                                const ImageView& visibility1ImageView,
                                                const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
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
            VkDescriptorImageInfo Info3 = {NULL, albedoBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info4 = {NULL, normalBufferImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info5 = {NULL, visibility0ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo Info6 = {NULL, visibility1ImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
                descriptorSets.Bind(i, 1, Info1),
                descriptorSets.Bind(i, 2, uniformBufferInfo),
                descriptorSets.Bind(i, 3, Info3),
                descriptorSets.Bind(i, 4, Info4),
                descriptorSets.Bind(i, 5, Info5),
                descriptorSets.Bind(i, 6, Info6),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/FinalCompose.comp.slang.spv");

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

    
    SimpleComposePipeline::SimpleComposePipeline(const SwapChain& swapChain,
                                               const ImageView& sourceImageView,
                                                const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
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

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/SimpleCompose.comp.slang.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
    }

    SimpleComposePipeline::~SimpleComposePipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet SimpleComposePipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    BufferClearPipeline::BufferClearPipeline(const SwapChain& swapChain):swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, swapChain.ImageViews().size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info0 = {NULL, swapChain.ImageViews()[i]->Handle(), VK_IMAGE_LAYOUT_GENERAL};

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, Info0),
            };
            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, "assets/shaders/BufferClear.comp.slang.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create buffer clear pipeline");
    }

    BufferClearPipeline::~BufferClearPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet BufferClearPipeline::DescriptorSet(uint32_t index) const
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
        const ShaderModule denoiseShader(device, "assets/shaders/VisualDebugger.comp.slang.spv");

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

    RayCastPipeline::RayCastPipeline(const DeviceProcedures& deviceProcedures, const Buffer& ioBuffer,
        const RayTracing::TopLevelAccelerationStructure& accelerationStructure, const Assets::Scene& scene):deviceProcedures_(deviceProcedures)
    {
        // Create descriptor pool/sets.
        const auto& device = deviceProcedures_.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            //{3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            // Vertex buffer, Index buffer, Material buffer, Offset buffer
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        // Top level acceleration structure.
        const auto accelerationStructureHandle = accelerationStructure.Handle();
        VkWriteDescriptorSetAccelerationStructureKHR structureInfo = {};
        structureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        structureInfo.pNext = nullptr;
        structureInfo.accelerationStructureCount = 1;
        structureInfo.pAccelerationStructures = &accelerationStructureHandle;
        
        VkDescriptorBufferInfo ioBufferInfo = {};
        ioBufferInfo.buffer = ioBuffer.Handle();
        ioBufferInfo.range = VK_WHOLE_SIZE;

        // Uniform buffer
        // VkDescriptorBufferInfo uniformBufferInfo = {};
        // uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
        // uniformBufferInfo.range = VK_WHOLE_SIZE;

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
            descriptorSets.Bind(0, 0, structureInfo),
            descriptorSets.Bind(0, 1, ioBufferInfo),
            descriptorSets.Bind(0, 4, vertexBufferInfo),
            descriptorSets.Bind(0, 5, indexBufferInfo),
            descriptorSets.Bind(0, 6, materialBufferInfo),
            descriptorSets.Bind(0, 7, offsetsBufferInfo),
            descriptorSets.Bind(0, 8, nodesBufferInfo),
        };

        descriptorSets.UpdateDescriptors(0, descriptorWrites);
        
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        const ShaderModule denoiseShader(device, "assets/shaders/RayCast.comp.slang.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create ray-cast shading pipeline");
    }

    RayCastPipeline::~RayCastPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(deviceProcedures_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet RayCastPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    DirectLightGenPipeline::DirectLightGenPipeline(const SwapChain& swapChain, const DeviceProcedures& deviceProcedures,
        const RayTracing::TopLevelAccelerationStructure& accelerationStructure, const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):deviceProcedures_(deviceProcedures)
    {
                // Create descriptor pool/sets.
        const auto& device = deviceProcedures_.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
            
            {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            // Vertex buffer, Index buffer, Material buffer, Offset buffer
            {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {9, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},

            {10, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {11, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));

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

            // Light buffer
            VkDescriptorBufferInfo lightBufferInfo = {};
            lightBufferInfo.buffer = scene.LightBuffer().Handle();
            lightBufferInfo.range = VK_WHOLE_SIZE;

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

            VkDescriptorBufferInfo shBufferInfo = {};
            shBufferInfo.buffer = scene.HDRSHBuffer().Handle();
            shBufferInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo probeBufferInfo = {};
            probeBufferInfo.buffer = scene.AmbientCubeBuffer().Handle();
            probeBufferInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo voxBufferInfo = {};
            voxBufferInfo.buffer = scene.FarAmbientCubeBuffer().Handle();
            voxBufferInfo.range = VK_WHOLE_SIZE;
            
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(0, 0, structureInfo),
                
                descriptorSets.Bind(0, 2, lightBufferInfo),
                descriptorSets.Bind(0, 3, uniformBufferInfo),
                descriptorSets.Bind(0, 4, vertexBufferInfo),
                descriptorSets.Bind(0, 5, indexBufferInfo),
                descriptorSets.Bind(0, 6, materialBufferInfo),
                descriptorSets.Bind(0, 7, offsetsBufferInfo),
                descriptorSets.Bind(0, 8, nodesBufferInfo),
                descriptorSets.Bind(0, 9, shBufferInfo),

                descriptorSets.Bind(0, 10, probeBufferInfo),
                descriptorSets.Bind(0, 11, voxBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/AmbientCubeGen.comp.slang.spv");
 
        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create direct lighting gen shading pipeline");
    }

    DirectLightGenPipeline::~DirectLightGenPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(deviceProcedures_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet DirectLightGenPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }
}
