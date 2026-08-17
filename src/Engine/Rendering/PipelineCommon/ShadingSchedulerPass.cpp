#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/ShadingSchedulerPass.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

#include <array>

namespace Vulkan::PipelineCommon
{
    namespace
    {
        // Scheduler.kHeaderUints / kEntryUints in Shader/ShadingScheduler.slang.
        constexpr uint32_t kHeaderUints = 4u * ShadingSchedulerPass::kBucketCount;
        constexpr uint32_t kEntryUints = 3u;

        uint32_t TileCount(const VkExtent2D extent)
        {
            const uint32_t tilesX = (extent.width + ShadingSchedulerPass::kTileSize - 1u) /
                ShadingSchedulerPass::kTileSize;
            const uint32_t tilesY = (extent.height + ShadingSchedulerPass::kTileSize - 1u) /
                ShadingSchedulerPass::kTileSize;
            return tilesX * tilesY;
        }
    }

    ShadingSchedulerPass::ShadingSchedulerPass() = default;
    ShadingSchedulerPass::~ShadingSchedulerPass() = default;

    void ShadingSchedulerPass::CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene)
    {
        classifyPipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Task.ShadingClassify.comp.slang.spv", scene);
        finalizePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Task.ShadingClassifyFinalize.comp.slang.spv", scene);
    }

    void ShadingSchedulerPass::DeleteSwapChain()
    {
        classifyPipeline_.reset();
        finalizePipeline_.reset();
        buffer_.reset();
        memory_.reset();
        retired_.clear();
        maxTiles_ = 0;
    }

    void ShadingSchedulerPass::EnsureResources(
        VulkanBaseRenderer& baseRender, const VkExtent2D extent)
    {
        // maxTiles_ is a capacity, not the current tile count: one renderer serves the primary view
        // and every secondary RenderView (thumbnails, offscreen cameras, reference views), and those
        // extents alternate within a single frame. Sizing to the largest extent seen makes the
        // buffer stop churning after the first frame; a smaller view simply leaves the tail unused.
        const uint32_t neededTiles = TileCount(extent);
        if (buffer_ && neededTiles <= maxTiles_)
        {
            return;
        }

        // The superseded buffer may still be referenced by commands recorded earlier this frame, so
        // it cannot be destroyed here. Growth is bounded by the number of distinct larger extents,
        // and DeleteSwapChain (device idle) frees the whole set.
        if (buffer_)
        {
            retired_.push_back({std::move(memory_), std::move(buffer_)});
        }
        maxTiles_ = neededTiles;

        const size_t bytes =
            sizeof(uint32_t) * (kHeaderUints + size_t(kBucketCount) * maxTiles_ * kEntryUints);
        BufferUtil::CreateDeviceBufferLocal(
            baseRender.CommandPool(), "ShadingSchedulerTiles",
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bytes, buffer_, memory_);

        SPDLOG_INFO("Shading scheduler tile lists allocated: {}x{} ({} tiles, {:.2f} MiB)",
                    extent.width, extent.height, maxTiles_,
                    static_cast<double>(bytes) / (1024.0 * 1024.0));
    }

    void ShadingSchedulerPass::ApplyToScene(Assets::GPUScene& gpuScene) const
    {
        gpuScene.ReservedAddress0 = buffer_ ? buffer_->GetDeviceAddress() : 0;
        gpuScene.CustomData1 = maxTiles_;
    }

    void ShadingSchedulerPass::Classify(
        VulkanBaseRenderer& baseRender,
        VkCommandBuffer commandBuffer,
        const Assets::GPUScene& gpuScene)
    {
        if (!classifyPipeline_)
        {
            return;
        }
        const VkExtent2D extent = baseRender.ActiveViewRenderExtent();
        EnsureResources(baseRender, extent);
        if (!buffer_)
        {
            return;
        }

        SCOPED_GPU_TIMER("shading classify");

        // One buffer serves every view in the frame, so the reset below is a write-after-read
        // against the previous view's bucket dispatches - both their tile-list reads and their
        // indirect argument fetch have to complete first.
        VkBufferMemoryBarrier previousViewToTransfer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        previousViewToTransfer.srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        previousViewToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        previousViewToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        previousViewToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        previousViewToTransfer.buffer = buffer_->Handle();
        previousViewToTransfer.offset = 0;
        previousViewToTransfer.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                             &previousViewToTransfer, 0, nullptr);

        // Reset the three VkDispatchIndirectCommands. y and z stay 1; x is the tile counter the
        // classification atomically increments, so it doubles as the append cursor.
        std::array<uint32_t, kHeaderUints> header{};
        for (uint32_t bucket = 0; bucket < kBucketCount; ++bucket)
        {
            header[bucket * 3u + 0u] = 0u;
            header[bucket * 3u + 1u] = 1u;
            header[bucket * 3u + 2u] = 1u;
        }
        vkCmdUpdateBuffer(commandBuffer, buffer_->Handle(), 0,
                          static_cast<VkDeviceSize>(sizeof(header)), header.data());

        VkBufferMemoryBarrier transferToShader{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        transferToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        transferToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferToShader.buffer = buffer_->Handle();
        transferToShader.offset = 0;
        transferToShader.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                             &transferToShader, 0, nullptr);

        baseRender.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, ERenderStage::Compute, EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_BSDF_DATA, ERenderStage::Compute, EResourceAccess::ShaderRead},
        }, "shading classify");

        Assets::GPUScene classifyScene = gpuScene;
        ApplyToScene(classifyScene);
        classifyPipeline_->BindPipeline(commandBuffer, classifyScene);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(extent.width, kTileSize),
                      Utilities::Math::GetSafeDispatchCount(extent.height, kTileSize), 1);

        // The append cursor is only final once every classification workgroup has retired, so the
        // conversion into a dispatch (tile count out, ceil(n/2) groups in for the compacted bucket)
        // has to be its own pass. One workgroup of kBucketCount threads.
        VkBufferMemoryBarrier classifyToFinalize{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        classifyToFinalize.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        classifyToFinalize.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        classifyToFinalize.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        classifyToFinalize.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        classifyToFinalize.buffer = buffer_->Handle();
        classifyToFinalize.offset = 0;
        classifyToFinalize.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                             &classifyToFinalize, 0, nullptr);

        finalizePipeline_->BindPipeline(commandBuffer, classifyScene);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        // The tile lists are read as storage in the bucket kernels and as indirect arguments by the
        // dispatches themselves, so both destinations have to be made visible.
        VkBufferMemoryBarrier shaderToIndirect{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        shaderToIndirect.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        shaderToIndirect.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        shaderToIndirect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderToIndirect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderToIndirect.buffer = buffer_->Handle();
        shaderToIndirect.offset = 0;
        shaderToIndirect.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 0, nullptr, 1, &shaderToIndirect, 0, nullptr);
    }

    namespace
    {
        // The two pipeline classes share PipelineBase but not BindPipeline, so the bucket dispatch
        // is written once here and instantiated for each.
        template <typename TPipeline>
        void RecordBucketDispatch(VkCommandBuffer commandBuffer, TPipeline& pipeline,
                                  const Assets::GPUScene& bucketScene, Vulkan::Buffer& buffer,
                                  const ShadingSchedulerPass::EBucket bucket)
        {
            pipeline.BindPipeline(commandBuffer, bucketScene);
            const VkDeviceSize offset =
                static_cast<VkDeviceSize>(static_cast<uint32_t>(bucket)) * 3u * sizeof(uint32_t);
            vkCmdDispatchIndirect(commandBuffer, buffer.Handle(), offset);
        }
    }

    void ShadingSchedulerPass::DispatchBucket(
        VkCommandBuffer commandBuffer,
        ZeroBindPipeline& pipeline,
        const Assets::GPUScene& gpuScene,
        const EBucket bucket) const
    {
        if (!buffer_)
        {
            return;
        }
        Assets::GPUScene bucketScene = gpuScene;
        ApplyToScene(bucketScene);
        RecordBucketDispatch(commandBuffer, pipeline, bucketScene, *buffer_, bucket);
    }

    void ShadingSchedulerPass::DispatchBucket(
        VkCommandBuffer commandBuffer,
        ZeroBindWithTLASPipeline& pipeline,
        const Assets::GPUScene& gpuScene,
        const EBucket bucket) const
    {
        if (!buffer_)
        {
            return;
        }
        Assets::GPUScene bucketScene = gpuScene;
        ApplyToScene(bucketScene);
        RecordBucketDispatch(commandBuffer, pipeline, bucketScene, *buffer_, bucket);
    }
}
