#include "Engine/Rendering/Compatibility/CompatibilityRenderer.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan::Compatibility
{
    namespace
    {
        // The shader uses only 32-bit floats. This UNORM format avoids Float16 SPIR-V capabilities
        // and does not consume the typed storage-image arrays owned by the full renderer.
        constexpr VkFormat kGBufferFormat = VK_FORMAT_R8G8B8A8_UNORM;
        constexpr VkImageSubresourceRange kColorRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        constexpr uint32_t kSoftMeshGroupSize = 64;

        // Matches the uint4 item used by the compatibility cull/expand shaders. Keeping the
        // record as ordinary SSBO data is the key difference from SoftMeshShaderResources: no
        // field is a device address.
        struct FSoftMeshVisibleItem
        {
            uint32_t PrimitiveBase;
            uint32_t ProxyIndex;
            uint32_t TriangleCount;
            uint32_t Padding;
        };
        static_assert(sizeof(FSoftMeshVisibleItem) == 16);

        VkPipeline CreateComputePipeline(
            const Device& device,
            const ShaderModule& shader,
            const VkPipelineLayout layout,
            const char* const debugName)
        {
            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = shader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
            pipelineInfo.layout = layout;

            VkPipeline pipeline = VK_NULL_HANDLE;
            Check(vkCreateComputePipelines(device.Handle(), device.PipelineCache(), 1, &pipelineInfo,
                                           nullptr, &pipeline),
                  debugName);
            device.RecordPipelineCreated(debugName);
            return pipeline;
        }

        void DestroyPipeline(const Device& device, VkPipeline& pipeline)
        {
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device.Handle(), pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }

        void RequireCompatibilityImageSupport(const Device& device,
                                              const VkImageUsageFlags usage,
                                              const VkFormatFeatureFlags requiredFeatures,
                                              const char* const debugName)
        {
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(device.PhysicalDevice(), kGBufferFormat,
                                                 &formatProperties);
            if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
            {
                throw std::runtime_error(fmt::format(
                    "compatibility mini G-buffer image '{}' is unsupported: format {} has optimal features "
                    "0x{:x}, requires 0x{:x}",
                    debugName, static_cast<int>(kGBufferFormat), formatProperties.optimalTilingFeatures,
                    requiredFeatures));
            }

            VkImageFormatProperties imageFormatProperties{};
            const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
                device.PhysicalDevice(), kGBufferFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                usage, 0, &imageFormatProperties);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(fmt::format(
                    "compatibility mini G-buffer image '{}' is unsupported for usage 0x{:x} (VkResult {})",
                    debugName, usage, static_cast<int>(result)));
            }
        }
    }

    CompatibilityRenderer::~CompatibilityRenderer()
    {
        CompatibilityRenderer::DeleteSwapChain();
    }

    void CompatibilityRenderer::CreateSoftMeshScratchBuffers(const uint32_t triangleCapacity)
    {
        // Unlike Scene::SoftMeshShader* these allocations intentionally omit
        // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. They are reached solely through bindings
        // EB_ExpandedPrimitives..EB_DispatchArgs in the renderer-local descriptor set.
        const uint32_t safeTriangleCapacity = std::max(1u, triangleCapacity);
        const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferUtil::CreateDeviceBufferLocal(
            CommandPool(), "Compatibility SoftMesh Primitives", storageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t) * 2u * safeTriangleCapacity,
            softMeshPrimitiveBuffer_, softMeshPrimitiveMemory_);
        BufferUtil::CreateDeviceBufferLocal(
            CommandPool(), "Compatibility SoftMesh Visible Items", storageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(FSoftMeshVisibleItem) * Assets::Scene::kRenderProxyCapacity,
            softMeshVisibleItemBuffer_, softMeshVisibleItemMemory_);
        BufferUtil::CreateDeviceBufferLocal(
            CommandPool(), "Compatibility SoftMesh Counters",
            storageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(uint32_t) * 2u,
            softMeshCounterBuffer_, softMeshCounterMemory_);
        BufferUtil::CreateDeviceBufferLocal(
            CommandPool(), "Compatibility SoftMesh Draw Args",
            storageUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDrawIndirectCommand),
            softMeshDrawArgBuffer_, softMeshDrawArgMemory_);
        BufferUtil::CreateDeviceBufferLocal(
            CommandPool(), "Compatibility SoftMesh Dispatch Args",
            storageUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDispatchIndirectCommand),
            softMeshDispatchArgBuffer_, softMeshDispatchArgMemory_);
        softMeshTriangleCapacity_ = safeTriangleCapacity;
        boundBuffers_.fill(VK_NULL_HANDLE);
    }

    void CompatibilityRenderer::DeleteSoftMeshScratchBuffers()
    {
        softMeshDispatchArgBuffer_.reset();
        softMeshDispatchArgMemory_.reset();
        softMeshDrawArgBuffer_.reset();
        softMeshDrawArgMemory_.reset();
        softMeshCounterBuffer_.reset();
        softMeshCounterMemory_.reset();
        softMeshVisibleItemBuffer_.reset();
        softMeshVisibleItemMemory_.reset();
        softMeshPrimitiveBuffer_.reset();
        softMeshPrimitiveMemory_.reset();
        softMeshTriangleCapacity_ = 0;
        boundBuffers_.fill(VK_NULL_HANDLE);
    }

    void CompatibilityRenderer::EnsureSoftMeshScratchCapacity(const Assets::Scene& scene)
    {
        const uint32_t requiredCapacity = std::max(1u, scene.GetMaxSceneTriangles());
        if (requiredCapacity <= softMeshTriangleCapacity_)
        {
            return;
        }

        // Scene topology can grow without a swapchain recreation. This is rare, and an explicit
        // idle is preferable to freeing an indirect/SSBO allocation a previously submitted frame
        // may still read. It mirrors Scene::EnsureGpuDrivenBufferCapacity while remaining BDA-free.
        if (softMeshPrimitiveBuffer_)
        {
            Device().WaitIdle();
            DeleteSoftMeshScratchBuffers();
        }
        CreateSoftMeshScratchBuffers(requiredCapacity);
        SPDLOG_INFO("Compatibility soft-mesh triangle capacity grown to {}", requiredCapacity);
    }

    void CompatibilityRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        const auto& device = Device();
        const class SwapChain& swapChain = SwapChain();

        // Fixed descriptors only: GlobalTexturePool has no bindless storage-image array under
        // FBindlessProfile::Compatibility().
        RequireCompatibilityImageSupport(
            device, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
            "GBuffer");
        RequireCompatibilityImageSupport(
            device, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT,
            "SceneColor");

        gbufferAlbedo_ = std::make_unique<RenderImage>(
            device, swapChain.Extent(), kGBufferFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
            "Compatibility GBuffer Albedo");
        gbufferNormal_ = std::make_unique<RenderImage>(
            device, swapChain.Extent(), kGBufferFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
            "Compatibility GBuffer Normal");
        sceneColor_ = std::make_unique<RenderImage>(
            device, swapChain.Extent(), kGBufferFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false,
            "Compatibility Scene Color");
        gbufferInitialized_ = false;
        sceneColorInitialized_ = false;

        CreateSoftMeshScratchBuffers(GetScene().GetMaxSceneTriangles());

        // Storage buffers preserve the CPU-side std430 array stride; a UBO would pad
        // NodeProxy::matId[16] from four to sixteen bytes per element.
        const std::vector<DescriptorBinding> drawBindings = {
            {EB_Nodes, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_VertexWords, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_Indices, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_Offsets, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_Materials, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
            {EB_ExpandedPrimitives, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_VisibleItems, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_Counters, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_DrawArgs, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {EB_DispatchArgs, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        };
        drawDescriptorSetManager_ = std::make_unique<DescriptorSetManager>(device, drawBindings, 1);
        boundBuffers_.fill(VK_NULL_HANDLE);

        VkPushConstantRange drawPushConstantRange{};
        drawPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        drawPushConstantRange.size = sizeof(FDrawPushConstants);
        std::vector<DescriptorSetManager*> drawManagers = {drawDescriptorSetManager_.get()};
        drawPipelineLayout_ = std::make_unique<class PipelineLayout>(
            device, drawManagers, 1, &drawPushConstantRange, 1);

        VkPushConstantRange softMeshCullPushConstantRange{};
        softMeshCullPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        softMeshCullPushConstantRange.size = sizeof(FSoftMeshCullPushConstants);
        softMeshCullPipelineLayout_ = std::make_unique<class PipelineLayout>(
            device, drawManagers, 1, &softMeshCullPushConstantRange, 1);
        softMeshPipelineLayout_ = std::make_unique<class PipelineLayout>(device, drawManagers, 1);

        // Texture2D.Load() requires only sampled images, not samplers. This avoids both a sampler
        // descriptor and the sampled-texture bindless array.
        const std::vector<DescriptorBinding> shadeBindings = {
            {ESB_Albedo, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {ESB_Normal, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
            {ESB_SceneColor, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        };
        shadeDescriptorSetManager_ = std::make_unique<DescriptorSetManager>(device, shadeBindings, 1);
        {
            const std::array<VkDescriptorImageInfo, ESB_Count> imageInfos{
                VkDescriptorImageInfo{VK_NULL_HANDLE, gbufferAlbedo_->GetImageView().Handle(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                VkDescriptorImageInfo{VK_NULL_HANDLE, gbufferNormal_->GetImageView().Handle(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                VkDescriptorImageInfo{VK_NULL_HANDLE, sceneColor_->GetImageView().Handle(),
                                      VK_IMAGE_LAYOUT_GENERAL},
            };
            auto& descriptorSets = shadeDescriptorSetManager_->DescriptorSets();
            const std::vector<VkWriteDescriptorSet> writes = {
                descriptorSets.Bind(0, ESB_Albedo, imageInfos[ESB_Albedo]),
                descriptorSets.Bind(0, ESB_Normal, imageInfos[ESB_Normal]),
                descriptorSets.Bind(0, ESB_SceneColor, imageInfos[ESB_SceneColor]),
            };
            descriptorSets.UpdateDescriptors(0, writes);
        }

        VkPushConstantRange shadePushConstantRange{};
        shadePushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        shadePushConstantRange.size = sizeof(FShadePushConstants);
        std::vector<DescriptorSetManager*> shadeManagers = {shadeDescriptorSetManager_.get()};
        shadePipelineLayout_ = std::make_unique<class PipelineLayout>(
            device, shadeManagers, 1, &shadePushConstantRange, 1);

        const std::array<VkFormat, 2> gbufferFormats = {kGBufferFormat, kGBufferFormat};
        gbufferRenderPass_ = std::make_unique<class RenderPass>(
            swapChain, std::span<const VkFormat>(gbufferFormats), baseRender_.DepthBuffer(),
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        gbufferRenderPass_->SetDebugName("Compatibility GBuffer Render Pass");
        const std::array<const ImageView*, 2> gbufferViews = {
            &gbufferAlbedo_->GetImageView(),
            &gbufferNormal_->GetImageView(),
        };
        gbufferFrameBuffer_ = std::make_unique<FrameBuffer>(
            swapChain.Extent(), gbufferViews, *gbufferRenderPass_);

        const ShaderModule vertShader(device, "assets/shaders/Rast.CompatibilitySoftMesh.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.CompatibilityGBuffer.frag.slang.spv");
        drawPipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertShader, fragShader)
            .SetDynamicViewportAndScissor()
            .SetDepth(true, true, VK_COMPARE_OP_GREATER)
            .SetColorAttachmentCount(static_cast<uint32_t>(gbufferFormats.size()))
            .Build(drawPipelineLayout_->Handle(), gbufferRenderPass_->Handle(),
                   "create compatibility G-buffer graphics pipeline");

        const ShaderModule softMeshCullShader(
            device, "assets/shaders/Task.CompatibilitySoftMeshCullCompact.comp.slang.spv");
        softMeshCullPipeline_ = CreateComputePipeline(
            device, softMeshCullShader, softMeshCullPipelineLayout_->Handle(),
            "create compatibility soft-mesh cull compute pipeline");
        const ShaderModule softMeshFinalizeShader(
            device, "assets/shaders/Task.CompatibilitySoftMeshFinalize.comp.slang.spv");
        softMeshFinalizePipeline_ = CreateComputePipeline(
            device, softMeshFinalizeShader, softMeshCullPipelineLayout_->Handle(),
            "create compatibility soft-mesh finalize compute pipeline");
        const ShaderModule softMeshExpandShader(
            device, "assets/shaders/Task.CompatibilitySoftMeshExpand.comp.slang.spv");
        softMeshExpandPipeline_ = CreateComputePipeline(
            device, softMeshExpandShader, softMeshPipelineLayout_->Handle(),
            "create compatibility soft-mesh expand compute pipeline");

        const ShaderModule shadeShader(device, "assets/shaders/Core.CompatibilityShade.comp.slang.spv");
        shadePipeline_ = CreateComputePipeline(
            device, shadeShader, shadePipelineLayout_->Handle(),
            "create compatibility shade compute pipeline");
    }

    void CompatibilityRenderer::DeleteSwapChain()
    {
        gbufferFrameBuffer_.reset();
        DestroyPipeline(Device(), drawPipeline_);
        DestroyPipeline(Device(), softMeshCullPipeline_);
        DestroyPipeline(Device(), softMeshFinalizePipeline_);
        DestroyPipeline(Device(), softMeshExpandPipeline_);
        DestroyPipeline(Device(), shadePipeline_);
        gbufferRenderPass_.reset();
        shadePipelineLayout_.reset();
        shadeDescriptorSetManager_.reset();
        softMeshPipelineLayout_.reset();
        softMeshCullPipelineLayout_.reset();
        drawPipelineLayout_.reset();
        drawDescriptorSetManager_.reset();
        DeleteSoftMeshScratchBuffers();
        sceneColor_.reset();
        gbufferNormal_.reset();
        gbufferAlbedo_.reset();
        gbufferInitialized_ = false;
        sceneColorInitialized_ = false;
    }

    void CompatibilityRenderer::BindSceneBuffers(const Assets::Scene& scene)
    {
        // Nodes and Materials are two windows onto the same scene-dynamic buffer; the offsets are
        // the ones the shaders would otherwise reach by pointer arithmetic off SceneDynamicBase.
        const VkBuffer sceneDynamic = scene.NodeMatrixBuffer().Handle();
        const std::array<VkDescriptorBufferInfo, EB_Count> bufferInfos{
            VkDescriptorBufferInfo{sceneDynamic, Assets::GPU_SCENE_DYNAMIC_NODES_OFFSET,
                                   Assets::GPU_SCENE_NODE_PROXY_SIZE * Assets::MAX_RENDER_PROXIES},
            VkDescriptorBufferInfo{scene.VertexBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{scene.PrimAddressBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{scene.OffsetBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{sceneDynamic, Assets::GPU_SCENE_DYNAMIC_MATERIALS_OFFSET,
                                   Assets::GPU_SCENE_MATERIAL_SIZE * Assets::MAX_MATERIALS},
            VkDescriptorBufferInfo{softMeshPrimitiveBuffer_->Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{softMeshVisibleItemBuffer_->Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{softMeshCounterBuffer_->Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{softMeshDrawArgBuffer_->Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{softMeshDispatchArgBuffer_->Handle(), 0, VK_WHOLE_SIZE},
        };

        bool changed = false;
        for (uint32_t binding = 0; binding < EB_Count; ++binding)
        {
            changed = changed || boundBuffers_[binding] != bufferInfos[binding].buffer;
        }
        if (!changed)
        {
            return;
        }

        auto& descriptorSets = drawDescriptorSetManager_->DescriptorSets();
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(EB_Count);
        for (uint32_t binding = 0; binding < EB_Count; ++binding)
        {
            writes.push_back(descriptorSets.Bind(0, binding, bufferInfos[binding]));
            boundBuffers_[binding] = bufferInfos[binding].buffer;
        }
        descriptorSets.UpdateDescriptors(0, writes);
    }

    void CompatibilityRenderer::DispatchSoftMesh(
        const VkCommandBuffer commandBuffer,
        const Assets::Scene& scene)
    {
        // NodeProxy data is host-updated after the previous frame fence. Both culling and the
        // later vertex pulling read that same fixed SSBO, so make the host writes visible to both.
        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            scene.NodeMatrixBuffer().Handle(), VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // This scratch is recycled every frame. Complete every old consumer before overwriting
        // it, but do not clear the large primitive/item buffers: the generated indirect counts
        // ensure the following passes only read records written during this frame.
        BufferMemoryBarrier::Insert(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {
                BufferMemoryBarrier::Make(
                    softMeshPrimitiveBuffer_->Handle(),
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT),
                BufferMemoryBarrier::Make(
                    softMeshVisibleItemBuffer_->Handle(),
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT),
                BufferMemoryBarrier::Make(
                    softMeshDrawArgBuffer_->Handle(),
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT),
                BufferMemoryBarrier::Make(
                    softMeshDispatchArgBuffer_->Handle(),
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT),
            });
        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, softMeshCounterBuffer_->Handle(),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(commandBuffer, softMeshCounterBuffer_->Handle(), 0, VK_WHOLE_SIZE, 0);
        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            softMeshCounterBuffer_->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        const auto& proxies = scene.GetNodeProxies();
        const VkExtent2D outputExtent = SwapChain().OutputExtent();
        const FSoftMeshCullPushConstants pushConstants{
            .ViewProjection = baseRender_.LastUniformBufferObject().ViewProjection,
            .RenderProxyCount = std::min(
                static_cast<uint32_t>(proxies.size()), Assets::Scene::kRenderProxyCapacity),
            .TriangleCapacity = softMeshTriangleCapacity_,
            .ViewportSize = glm::vec2(static_cast<float>(outputExtent.width),
                                      static_cast<float>(outputExtent.height)),
            .LodBaseThreshold = NextEngine::GetInstance()->GetUserSettings().LodBaseThreshold,
            .Padding = 0.0f,
        };

        // This is the non-BDA counterpart of Task.SoftMeshShaderGpuCullCompact: it frustum-culls
        // proxies and reserves a gapless primitive range plus one visible-item record per survivor.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, softMeshCullPipeline_);
        softMeshCullPipelineLayout_->BindDescriptorSets(
            commandBuffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdPushConstants(
            commandBuffer, softMeshCullPipelineLayout_->Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(pushConstants), &pushConstants);
        if (pushConstants.RenderProxyCount > 0)
        {
            vkCmdDispatch(commandBuffer,
                          (pushConstants.RenderProxyCount + kSoftMeshGroupSize - 1u) /
                              kSoftMeshGroupSize,
                          1, 1);
        }

        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {
                BufferMemoryBarrier::Make(
                    softMeshCounterBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(
                    softMeshVisibleItemBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT),
            });

        // The one-thread finalizer produces both the graphics indirect command and the dispatch
        // command for expansion. Its triangle clamp keeps a malformed scene from reading beyond
        // the private primitive buffer even if a stale proxy appears during a scene transition.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, softMeshFinalizePipeline_);
        softMeshCullPipelineLayout_->BindDescriptorSets(
            commandBuffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdPushConstants(
            commandBuffer, softMeshCullPipelineLayout_->Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(pushConstants), &pushConstants);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            {
                BufferMemoryBarrier::Make(
                    softMeshVisibleItemBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(
                    softMeshDispatchArgBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
                BufferMemoryBarrier::Make(
                    softMeshDrawArgBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
            });

        // One workgroup expands one compacted proxy. The shader distributes that proxy's triangles
        // across 64 lanes, matching the full soft-mesh expander without address arithmetic.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, softMeshExpandPipeline_);
        softMeshPipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdDispatchIndirect(commandBuffer, softMeshDispatchArgBuffer_->Handle(), 0);

        BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            {
                BufferMemoryBarrier::Make(
                    softMeshPrimitiveBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(
                    softMeshDrawArgBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
            });
    }

    void CompatibilityRenderer::TransitionGBufferForRaster(const VkCommandBuffer commandBuffer)
    {
        const VkPipelineStageFlags srcStage = gbufferInitialized_
            ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        const VkAccessFlags srcAccess = gbufferInitialized_ ? VK_ACCESS_SHADER_READ_BIT : 0;
        const VkImageLayout oldLayout = gbufferInitialized_
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        for (const RenderImage* image : {gbufferAlbedo_.get(), gbufferNormal_.get()})
        {
            ImageMemoryBarrier::Insert(
                commandBuffer, srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                image->GetImage().Handle(), kColorRange, srcAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
    }

    void CompatibilityRenderer::TransitionGBufferForShading(const VkCommandBuffer commandBuffer)
    {
        for (const RenderImage* image : {gbufferAlbedo_.get(), gbufferNormal_.get()})
        {
            ImageMemoryBarrier::Insert(
                commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, image->GetImage().Handle(), kColorRange,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        gbufferInitialized_ = true;
    }

    void CompatibilityRenderer::TransitionSceneColorForShading(const VkCommandBuffer commandBuffer)
    {
        ImageMemoryBarrier::Insert(
            commandBuffer,
            sceneColorInitialized_ ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, sceneColor_->GetImage().Handle(), kColorRange,
            sceneColorInitialized_ ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_SHADER_WRITE_BIT,
            sceneColorInitialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        sceneColorInitialized_ = true;
    }

    void CompatibilityRenderer::Render(const VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (drawPipeline_ == VK_NULL_HANDLE || softMeshCullPipeline_ == VK_NULL_HANDLE ||
            softMeshFinalizePipeline_ == VK_NULL_HANDLE || softMeshExpandPipeline_ == VK_NULL_HANDLE ||
            shadePipeline_ == VK_NULL_HANDLE || softMeshPrimitiveBuffer_ == nullptr ||
            softMeshVisibleItemBuffer_ == nullptr || softMeshCounterBuffer_ == nullptr ||
            softMeshDrawArgBuffer_ == nullptr || softMeshDispatchArgBuffer_ == nullptr ||
            gbufferFrameBuffer_ == nullptr || imageIndex >= SwapChain().Images().size())
        {
            return;
        }

        const Assets::Scene& scene = GetScene();
        EnsureSoftMeshScratchCapacity(scene);
        BindSceneBuffers(scene);
        const class SwapChain& swapChain = SwapChain();

        {
            SCOPED_GPU_TIMER("compatibility soft-mesh submit");
            DispatchSoftMesh(commandBuffer, scene);
        }

        {
            SCOPED_GPU_TIMER("compatibility G-buffer");
            TransitionGBufferForRaster(commandBuffer);

            // The compute pass owns the visible background. Normal.a = 0 is an unambiguous
            // uncovered-surface sentinel and makes albedo irrelevant outside drawn geometry.
            const std::array<VkClearValue, 3> clearValues{
                VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}},
                VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}},
                VkClearValue{.depthStencil = {0.0f, 0}},
            };
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = gbufferRenderPass_->Handle();
            renderPassInfo.framebuffer = gbufferFrameBuffer_->Handle();
            renderPassInfo.renderArea.extent = swapChain.Extent();
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                const VkViewport viewport{
                    static_cast<float>(swapChain.OutputOffset().x),
                    static_cast<float>(swapChain.OutputOffset().y),
                    static_cast<float>(swapChain.OutputExtent().width),
                    static_cast<float>(swapChain.OutputExtent().height),
                    0.0f, 1.0f};
                const VkRect2D scissor{swapChain.OutputOffset(), swapChain.OutputExtent()};
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, drawPipeline_);
                drawPipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);

                const FDrawPushConstants pushConstants{
                    .ViewProjection = baseRender_.LastUniformBufferObject().ViewProjection,
                };
                vkCmdPushConstants(commandBuffer, drawPipelineLayout_->Handle(), VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(pushConstants), &pushConstants);
                vkCmdDrawIndirect(commandBuffer, softMeshDrawArgBuffer_->Handle(), 0, 1,
                                  sizeof(VkDrawIndirectCommand));
            }
            vkCmdEndRenderPass(commandBuffer);
        }

        {
            SCOPED_GPU_TIMER("compatibility shade");
            TransitionGBufferForShading(commandBuffer);
            TransitionSceneColorForShading(commandBuffer);

            const Assets::UniformBufferObject& camera = baseRender_.LastUniformBufferObject();
            const FShadePushConstants pushConstants{
                .SunDirection = camera.SunDirection,
                .SunColor = glm::vec4(glm::vec3(camera.SunColor), camera.HasSun ? 1.0f : 0.0f),
                .SkyColor = glm::vec4(glm::vec3(camera.SkyColor), camera.HasSky ? 1.0f : 0.0f),
            };
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadePipeline_);
            shadePipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
            vkCmdPushConstants(commandBuffer, shadePipelineLayout_->Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pushConstants), &pushConstants);
            const VkExtent2D extent = swapChain.Extent();
            vkCmdDispatch(commandBuffer, (extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1);
        }

        // The mini chain owns its intermediates, while the base renderer still owns swapchain
        // tracking and the final PRESENT transition for the UI pass.
        ImageMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            sceneColor_->GetImage().Handle(), kColorRange, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             .discardPreviousContents = true},
            "compatibility shade resolve");

        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[1] = {
            static_cast<int32_t>(swapChain.Extent().width),
            static_cast<int32_t>(swapChain.Extent().height),
            1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstOffsets[1] = blitRegion.srcOffsets[1];
        vkCmdBlitImage(commandBuffer, sceneColor_->GetImage().Handle(), VK_IMAGE_LAYOUT_GENERAL,
                        swapChain.Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                        &blitRegion, VK_FILTER_NEAREST);
    }
}
