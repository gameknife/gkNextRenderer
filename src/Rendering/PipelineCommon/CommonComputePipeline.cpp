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
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"

namespace Vulkan::PipelineCommon
{
    AccumulatePipeline::AccumulatePipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender, 
                                           const ImageView& accumulateImageView,
                                           const std::vector<Assets::UniformBuffer>& uniformBuffers,
                                           const Assets::Scene& scene): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorImageInfo Info1 = {NULL, accumulateImageView.Handle(), VK_IMAGE_LAYOUT_GENERAL};
            
            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, uniformBufferInfo),
                descriptorSets.Bind(i, 1, Info1),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        std::vector<DescriptorSetManager*> managers = {
            descriptorSetManager_.get(),
            &baseRender.GetRTDescriptorSetManager(),
        };
        pipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size())));
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

    FinalComposePipeline::FinalComposePipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender,
        const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, swapChain.ImageViews().size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;
            VkDescriptorImageInfo Info1 = {NULL, baseRender.rtDenoised->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL};
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, uniformBufferInfo),
                descriptorSets.Bind(i, 1, Info1),
            };
            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        std::vector<DescriptorSetManager*> managers = {
            descriptorSetManager_.get(),
            &baseRender.GetRTDescriptorSetManager()
        };
        
        pipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size()), &pushConstantRange, 1));
        
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
        
        pipelineLayout_.reset(new class PipelineLayout(device, {descriptorSetManager_.get()}, static_cast<uint32_t>(swapChain.Images().size()), &pushConstantRange, 1));
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

    BufferClearPipeline::BufferClearPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender):swapChain_(swapChain)
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

        pipelineLayout_.reset(new class PipelineLayout(device, {descriptorSetManager_.get(), &baseRender.GetRTDescriptorSetManager()}, static_cast<uint32_t>(swapChain.Images().size())));
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

    VisualDebuggerPipeline::VisualDebuggerPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender, const std::vector<Assets::UniformBuffer>& uniformBuffers): swapChain_(swapChain)
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

        pipelineLayout_.reset(new class PipelineLayout(device, {descriptorSetManager_.get(), &baseRender.GetRTDescriptorSetManager()},static_cast<uint32_t>(swapChain.Images().size())));
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

    HardwareGPULightBakePipeline::HardwareGPULightBakePipeline(const SwapChain& swapChain, const DeviceProcedures& deviceProcedures,
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
            indexBufferInfo.buffer = scene.PrimAddressBuffer().Handle();
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

    HardwareGPULightBakePipeline::~HardwareGPULightBakePipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(deviceProcedures_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet HardwareGPULightBakePipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    
    SoftwareGPULightBakePipeline::SoftwareGPULightBakePipeline(const SwapChain& swapChain, const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
    {
                // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
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
            {14, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
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
            
            VkDescriptorBufferInfo pageBufferInfo = {};
            pageBufferInfo.buffer = scene.PageIndexBuffer().Handle();
            pageBufferInfo.range = VK_WHOLE_SIZE;
            
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
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
                
                descriptorSets.Bind(0, 14, pageBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/SoftAmbientCubeGen.comp.slang.spv");
 
        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create direct lighting gen shading pipeline");
    }

    SoftwareGPULightBakePipeline::~SoftwareGPULightBakePipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet SoftwareGPULightBakePipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    
    GPUCullPipeline::GPUCullPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender, const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):swapChain_(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, static_cast<uint32_t>(uniformBuffers.size())));
        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            VkDescriptorBufferInfo drawCmdBufferInfo = {};
            drawCmdBufferInfo.buffer = scene.IndirectDrawBuffer().Handle();
            drawCmdBufferInfo.range = VK_WHOLE_SIZE;
            
            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;
            
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, drawCmdBufferInfo),
                descriptorSets.Bind(i, 1, uniformBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }
        

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 8;
        
        pipelineLayout_.reset(new class PipelineLayout(device, {descriptorSetManager_.get(), &scene.GetSceneBufferDescriptorSetManager(), &baseRender.GetRTDescriptorSetManager()}, static_cast<uint32_t>(uniformBuffers.size()), &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/GpuCull.comp.slang.spv");

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();

        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create deferred shading pipeline");
    }

    GPUCullPipeline::~GPUCullPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet GPUCullPipeline::DescriptorSet(uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    VisibilityPipeline::VisibilityPipeline(
        const SwapChain& swapChain,
        const DepthBuffer& depthBuffer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers,
        const Assets::Scene& scene) :
        swapChain_(swapChain)
    {
        const auto& device = swapChain.Device();
        //const auto bindingDescription = Assets::GPUVertex::GetBindingDescription();
        //const auto attributeDescriptions = Assets::GPUVertex::GetFastAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        //vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        //vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        //vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.RenderExtent().width);
        viewport.height = static_cast<float>(swapChain.RenderExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = swapChain.RenderExtent();

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {}; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        // Create descriptor pool/sets.
        std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
			{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
			{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            // Nodes buffer
            VkDescriptorBufferInfo nodesBufferInfo = {};
            nodesBufferInfo.buffer = scene.NodeMatrixBuffer().Handle();
            nodesBufferInfo.range = VK_WHOLE_SIZE;

        	VkDescriptorBufferInfo reorderBufferInfo = {};
        	reorderBufferInfo.buffer = scene.ReorderBuffer().Handle();
        	reorderBufferInfo.range = VK_WHOLE_SIZE;

        	VkDescriptorBufferInfo vertexBufferInfo = {};
        	vertexBufferInfo.buffer = scene.SimpleVertexBuffer().Handle();
        	vertexBufferInfo.range = VK_WHOLE_SIZE;
        	
            const std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, uniformBufferInfo),
                descriptorSets.Bind(i, 1, nodesBufferInfo),
            	descriptorSets.Bind(i, 2, reorderBufferInfo),
            	descriptorSets.Bind(i, 3, vertexBufferInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        // Create pipeline layout and render pass.
        pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout()));
        renderPass_.reset(new class RenderPass(swapChain, VK_FORMAT_R16G16_UINT, depthBuffer, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR));
        renderPass_->SetDebugName("Visibility Render Pass");
        // Load shaders.
        const ShaderModule vertShader(device, "assets/shaders/VisibilityPass.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/VisibilityPass.frag.slang.spv");

        VkPipelineShaderStageCreateInfo shaderStages[] =
        {
            vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
            fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        // Create graphic pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr; // Optional
        pipelineInfo.basePipelineHandle = nullptr; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional
        pipelineInfo.layout = pipelineLayout_->Handle();
        pipelineInfo.renderPass = renderPass_->Handle();
        pipelineInfo.subpass = 0;

        Check(vkCreateGraphicsPipelines(device.Handle(), nullptr, 1, &pipelineInfo, nullptr, &pipeline_),
              "create graphics pipeline");
    }

    VisibilityPipeline::~VisibilityPipeline()
    {
        if (pipeline_ != nullptr)
        {
            vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
            pipeline_ = nullptr;
        }

        renderPass_.reset();
        pipelineLayout_.reset();
        descriptorSetManager_.reset();
    }

    VkDescriptorSet VisibilityPipeline::DescriptorSet(const uint32_t index) const
    {
        return descriptorSetManager_->DescriptorSets().Handle(index);
    }

    GraphicsPipeline::GraphicsPipeline(
	const SwapChain& swapChain, 
	const DepthBuffer& depthBuffer,
	const std::vector<Assets::UniformBuffer>& uniformBuffers,
	const Assets::Scene& scene,
	const bool isWireFrame) :
	swapChain_(swapChain),
	isWireFrame_(isWireFrame)
	{
		const auto& device = swapChain.Device();
		const auto bindingDescription = Assets::GPUVertex::GetBindingDescription();
		const auto attributeDescriptions = Assets::GPUVertex::GetFastAttributeDescriptions();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChain.Extent().width);
		viewport.height = static_cast<float>(swapChain.Extent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChain.Extent();

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = isWireFrame ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.minDepthBounds = 0.0f; // Optional
		depthStencil.maxDepthBounds = 1.0f; // Optional
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = {}; // Optional
		depthStencil.back = {}; // Optional

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // Optional
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		// Create descriptor pool/sets.
		std::vector<DescriptorBinding> descriptorBindings =
		{
			{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
		};

		descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

		auto& descriptorSets = descriptorSetManager_->DescriptorSets();

		for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
		{
			// Uniform buffer
			VkDescriptorBufferInfo uniformBufferInfo = {};
			uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
			uniformBufferInfo.range = VK_WHOLE_SIZE;

			// Nodes buffer
			VkDescriptorBufferInfo nodesBufferInfo = {};
			nodesBufferInfo.buffer = scene.NodeMatrixBuffer().Handle();
			nodesBufferInfo.range = VK_WHOLE_SIZE;

			const std::vector<VkWriteDescriptorSet> descriptorWrites =
			{
				descriptorSets.Bind(i, 0, uniformBufferInfo),
				descriptorSets.Bind(i, 1, nodesBufferInfo),
			};

			descriptorSets.UpdateDescriptors(i, descriptorWrites);
		}

		VkPushConstantRange pushConstantRange{};
		// Push constants will only be accessible at the selected pipeline stages, for this sample it's the vertex shader that reads them
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = 4 * 16;
		
		// Create pipeline layout and render pass.
		pipelineLayout_.reset(new class PipelineLayout(device, descriptorSetManager_->DescriptorSetLayout(), &pushConstantRange, 1));
		renderPass_.reset(new class RenderPass(swapChain, swapChain.Format(), depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR));
		renderPass_->SetDebugName("Wireframe Render Pass");

		// Load shaders.
		const ShaderModule vertShader(device, "assets/shaders/Graphics.vert.slang.spv");
		const ShaderModule fragShader(device, "assets/shaders/Graphics.frag.slang.spv");

		VkPipelineShaderStageCreateInfo shaderStages[] =
		{
			vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
			fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		// Create graphic pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = nullptr; // Optional
		pipelineInfo.basePipelineHandle = nullptr; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional
		pipelineInfo.layout = pipelineLayout_->Handle();
		pipelineInfo.renderPass = renderPass_->Handle();
		pipelineInfo.subpass = 0;

		Check(vkCreateGraphicsPipelines(device.Handle(), nullptr, 1, &pipelineInfo, nullptr, &pipeline_),
			"create graphics pipeline");
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		if (pipeline_ != nullptr)
		{
			vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
			pipeline_ = nullptr;
		}
    	
		renderPass_.reset();
		pipelineLayout_.reset();
		descriptorSetManager_.reset();
	}

	VkDescriptorSet GraphicsPipeline::DescriptorSet(const uint32_t index) const
	{
		return descriptorSetManager_->DescriptorSets().Handle(index);
	}
}
