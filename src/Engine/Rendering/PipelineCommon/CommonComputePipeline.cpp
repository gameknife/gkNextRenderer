#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/VisibilityBufferLayout.hpp"

#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Data/Vertex.hpp"

#include <algorithm>
#include <cstring>

namespace Vulkan::PipelineCommon
{
    namespace
    {
        // Shared tail of every zero-bind compute pipeline constructor
        VkPipeline CreateComputePipeline(const Device& device, const char* shaderfile, VkPipelineLayout layout)
        {
            const ShaderModule computeShader(device, shaderfile);

            VkComputePipelineCreateInfo pipelineCreateInfo = {};
            pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineCreateInfo.stage = computeShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
            pipelineCreateInfo.layout = layout;

            VkPipeline pipeline = VK_NULL_HANDLE;
            Check(vkCreateComputePipelines(device.Handle(), device.PipelineCache(), 1,
                &pipelineCreateInfo, NULL, &pipeline), shaderfile);
            device.RecordPipelineCreated(shaderfile);
            return pipeline;
        }

        // Shared bind + push-constant sequence of every zero-bind compute pipeline
        void BindComputeWithPush(VkCommandBuffer commandBuffer, VkPipeline pipeline,
            const class PipelineLayout& layout, uint32_t pushSize, const void* pushData)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            layout.BindDescriptorSets(commandBuffer, 0);
            vkCmdPushConstants(commandBuffer, layout.Handle(), VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
        }
    }

    ZeroBindWithTLASPipeline::ZeroBindWithTLASPipeline(
    const SwapChain& swapChain,
    const char* shaderfile,
    const Assets::Scene& scene,
    VkAccelerationStructureKHR accelerationStructureHandle):PipelineBase(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

#if ANDROID
        std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
        };

        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));
        auto& descriptorSets = descriptorSetManager_->DescriptorSets();

        VkWriteDescriptorSetAccelerationStructureKHR structureInfo = {};
        structureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        structureInfo.pNext = nullptr;
        structureInfo.accelerationStructureCount = 1;
        structureInfo.pAccelerationStructures = &accelerationStructureHandle;

        const std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, 0, structureInfo),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
#endif

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
#if ANDROID
            descriptorSetManager_.get()
#endif
        };

        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
    }

    void ZeroBindWithTLASPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene,
        uint32_t imageIndex)
    {
        BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene),
                            &(scene.FetchGPUScene(imageIndex, 0)));
    }

    void ZeroBindWithTLASPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
    {
        BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene), &gpuScene);
    }

    ZeroBindPipeline::ZeroBindPipeline(
    const SwapChain& swapChain,
    const char* shaderfile,
    const Assets::Scene& scene):PipelineBase(swapChain)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
        };

        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
    }

    void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex)
    {
        BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene),
                            &(scene.FetchGPUScene(imageIndex, 0)));
    }

    void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex,
                                        uint32_t customData0, uint32_t customData1, uint32_t customData2)
    {
        BindPipeline(commandBuffer, scene.FetchGPUScene(imageIndex, customData0), customData0, customData1, customData2);
    }

    void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
    {
        BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(Assets::GPUScene), &gpuScene);
    }

    void ZeroBindPipeline::BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene,
                                        uint32_t customData0, uint32_t customData1, uint32_t customData2)
    {
        Assets::GPUScene customGpuScene = gpuScene;
        customGpuScene.CustomData0 = customData0;
        customGpuScene.CustomData1 = customData1;
        customGpuScene.CustomData2 = customData2;
        BindPipeline(commandBuffer, customGpuScene);
    }

    ZeroBindCustomPushConstantPipeline::ZeroBindCustomPushConstantPipeline(const SwapChain& swapChain,
    const char* shaderfile, uint32_t pushConstantSize):PipelineBase(swapChain), pushConstantSize_(pushConstantSize)
    {
        // Create descriptor pool/sets.
        const auto& device = swapChain.Device();

        // Custom passes share the GPUScene push-constant block: the first 16 bytes are the unified
        // header (SwapChainIndex + CustomData0/1/2) that aliases gpuScene's header, so
        // gpuScene.CustomData0 (the active view RT bank base) is valid even though this pipeline
        // pushes a custom struct. The caller's params live right after the header (offset 16). Using
        // the full GPUScene size keeps the range identical to the gpuScene block the shader declares.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager()
        };

        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        pipeline_ = CreateComputePipeline(device, shaderfile, pipelineLayout_->Handle());
    }

    void ZeroBindCustomPushConstantPipeline::BindPipeline(
        VkCommandBuffer commandBuffer, const void* data, uint32_t viewBankBase)
    {
        // Stamp the unified header (offset 0..15), then the caller's params (offset 16..).
        static constexpr uint32_t kHeaderSize = 16;
        alignas(16) uint8_t buffer[sizeof(Assets::GPUScene)] = {};
        uint32_t* header = reinterpret_cast<uint32_t*>(buffer);
        header[0] = 0; // SwapChainIndex (unused by custom passes)
        header[1] = viewBankBase; // CustomData0
        header[2] = 0;
        header[3] = 0;
        const uint32_t copySize = std::min<uint32_t>(pushConstantSize_, sizeof(buffer) - kHeaderSize);
        if (data != nullptr && copySize > 0)
        {
            std::memcpy(buffer + kHeaderSize, data, copySize);
        }
        BindComputeWithPush(commandBuffer, Handle(), PipelineLayout(), sizeof(buffer), buffer);
    }

    VisibilityPipeline::VisibilityPipeline(
        const SwapChain& swapChain,
        const DepthBuffer& depthBuffer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers,
        const Assets::Scene& scene) :
        PipelineBase(swapChain)
    {
        const auto& device = swapChain.Device();

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
        };

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        // Create pipeline layout and render pass.
        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        const std::array visibilityFormats = {
            kVisibilityPlanes[0].format,
            kVisibilityPlanes[1].format,
        };
        renderPass_.reset(new class RenderPass(
            swapChain, std::span<const VkFormat>(visibilityFormats), depthBuffer,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR));
        renderPass_->SetDebugName("Visibility Render Pass");

        const ShaderModule vertShader(device, "assets/shaders/Rast.VisibilityPassSoftMeshShader.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.VisibilityPass.frag.slang.spv");

        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertShader, fragShader)
            .SetDynamicViewportAndScissor()
            .SetDepth(true, true, VK_COMPARE_OP_GREATER)
            .SetColorAttachmentCount(static_cast<uint32_t>(kVisibilityPlanes.size()))
            .Build(pipelineLayout_->Handle(), renderPass_->Handle(), "create graphics pipeline");
    }

    VisibilityPipeline::~VisibilityPipeline()
    {
        renderPass_.reset();
    }

    GraphicsPipeline::GraphicsPipeline(
    const SwapChain& swapChain,
    const DepthBuffer& depthBuffer,
    const std::vector<Assets::UniformBuffer>& uniformBuffers,
    const Assets::Scene& scene,
    const bool isWireFrame) :
    PipelineBase(swapChain)
    {
        (void)uniformBuffers;
        (void)scene;

        const auto& device = swapChain.Device();

        const VkOffset2D viewportOffset = swapChain.RenderOffset();
        const VkExtent2D viewportExtent = swapChain.RenderExtent();

        VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
        vkGetPhysicalDeviceFeatures(device.PhysicalDevice(), &physicalDeviceFeatures);

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
        };

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        // Create pipeline layout and render pass.
        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));
        const VkFormat colorFormat = isWireFrame ? VK_FORMAT_R16G16B16A16_SFLOAT : swapChain.Format();
        const VkImageLayout colorInitialLayout = isWireFrame
            ? VK_IMAGE_LAYOUT_GENERAL
            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        const VkImageLayout colorFinalLayout = isWireFrame
            ? VK_IMAGE_LAYOUT_GENERAL
            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        renderPass_.reset(new class RenderPass(
            swapChain, colorFormat, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD,
            colorInitialLayout, colorFinalLayout));
        renderPass_->SetDebugName("Wireframe Render Pass");

        const ShaderModule vertShader(device, "assets/shaders/Rast.WireframeSoftMeshShader.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.Wireframe.frag.slang.spv");

        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertShader, fragShader)
            .SetFixedViewport(viewportOffset, viewportExtent)
            .SetPolygonMode(isWireFrame && physicalDeviceFeatures.fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)
            .SetDepth(true, false, VK_COMPARE_OP_GREATER_OR_EQUAL)
            .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)
            .Build(pipelineLayout_->Handle(), renderPass_->Handle(), "create graphics pipeline");
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        renderPass_.reset();
    }
}
