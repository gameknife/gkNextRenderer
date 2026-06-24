#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"

#include <chrono>
#include <cstring>
#include <numeric>


#include "Engine/Runtime/Engine.hpp"

namespace Vulkan::PathTracing
{
    namespace
    {
        // Mirrors PushConsts in Process.ReProject.comp.slang (bool/uint/int are 4 bytes each in Slang
        // push constants). The trailing 3 floats are the Phase A history-clamp tuning knobs.
        struct FReprojectPushConstants
        {
            uint32_t ProgressiveRender;
            uint32_t TemporalFrames;
            uint32_t PrevDiffuseBindlessIdx;
            uint32_t PrevSpecularBindlessIdx;
            uint32_t PrevAlbedoBindlessIdx;
            uint32_t FastReproject;
            float ClampGammaHi;
            float ClampGammaLo;
            float ClampFloor;
        };

        constexpr uint32_t kSharcResolveThreadCount = 64;
        constexpr uint32_t kSharcMinEntriesPow2 = 10;
        constexpr uint32_t kSharcMaxEntriesPow2 = 22;
        constexpr uint32_t kSharcProbePadding = 16;

        static_assert(sizeof(Assets::SharcHashEntry) == 8);
        static_assert(sizeof(Assets::SharcAccumulationEntry) == 16);
        static_assert(sizeof(Assets::SharcResolvedEntry) == 16);
        static_assert(sizeof(Assets::SharcRuntimeParameters) == 64);

        void CreateSharcBuffer(
            Vulkan::CommandPool& commandPool,
            const char* name,
            VkDeviceSize size,
            VkMemoryPropertyFlags memoryFlags,
            PathTracingRenderer::FSharcBuffer& outBuffer)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(
                commandPool,
                name,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                memoryFlags,
                static_cast<size_t>(size),
                outBuffer.buffer,
                outBuffer.memory);
            outBuffer.size = size;
        }

        void WriteHostVisibleBuffer(PathTracingRenderer::FSharcBuffer& dst, const void* data, size_t size)
        {
            void* mapped = dst.memory->Map(0, size);
            std::memcpy(mapped, data, size);
            dst.memory->Unmap();
        }
    }

    PathTracingRenderer::~PathTracingRenderer()
    {
        PathTracingRenderer::DeleteSwapChain();
    }

    void PathTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        rayTracingPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline( SwapChain(), "assets/shaders/Core.PathTracing.comp.slang.spv", GetScene()));
        accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv", sizeof(FReprojectPushConstants)));
        atrousDenoiser_.CreateSwapChain(SwapChain());
        composePipelineNonDenoiser_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv", GetScene()));

        temporalResolve_.SetupHistory(baseRender_, {
            {PipelineCommon::ETemporalChannel::Diffuse, Assets::Bindless::RT_SINGLE_PREV_DIFFUSE, "prevDiffuseTmp"},
            {PipelineCommon::ETemporalChannel::Specular, Assets::Bindless::RT_SINGLE_PREV_SPECULAR, "prevSpecularTmp"},
            {PipelineCommon::ETemporalChannel::Albedo, Assets::Bindless::RT_SINGLE_PREV_ALBEDO, "prevAlbedoTmp"},
        });
    }

    void PathTracingRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        sharcUpdatePipeline_.reset();
        sharcResolvePipeline_.reset();
        sharcQueryPipeline_.reset();
        accumulatePipeline_.reset();
        atrousDenoiser_.DeleteSwapChain();
        composePipelineNonDenoiser_.reset();
    }

    void PathTracingRenderer::EnsureSharcPipelines()
    {
        if (!sharcUpdatePipeline_)
        {
            sharcUpdatePipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                SwapChain(), "assets/shaders/Core.SharcUpdate.comp.slang.spv", GetScene()));
        }
        if (!sharcResolvePipeline_)
        {
            sharcResolvePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
                SwapChain(), "assets/shaders/Core.SharcResolve.comp.slang.spv", GetScene()));
        }
        if (!sharcQueryPipeline_)
        {
            sharcQueryPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                SwapChain(), "assets/shaders/Core.SharcQuery.comp.slang.spv", GetScene()));
        }
    }

    bool PathTracingRenderer::IsOfflineProgressiveRenderActive() const
    {
        return NextEngine::GetInstance()->IsOfflineProgressivePathTracing();
    }

    bool PathTracingRenderer::IsEffectiveSharcEnabled() const
    {
        return NextEngine::GetInstance()->IsEffectiveSharcEnabled();
    }

    void PathTracingRenderer::EnsureSharcResources()
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const uint32_t entriesPow2 = std::clamp(settings.SharcEntriesPow2, kSharcMinEntriesPow2, kSharcMaxEntriesPow2);
        const uint32_t entryCount = 1u << entriesPow2;
        if (sharc_.entryCount == entryCount && sharc_.resources.buffer)
        {
            return;
        }

        sharc_ = {};
        sharc_.entriesPow2 = entriesPow2;
        sharc_.entryCount = entryCount;
        const uint32_t allocationEntryCount = entryCount + kSharcProbePadding;

        CreateSharcBuffer(CommandPool(), "SharcHashEntries",
                          sizeof(Assets::SharcHashEntry) * allocationEntryCount,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          sharc_.hashEntries);
        CreateSharcBuffer(CommandPool(), "SharcLockBuffer",
                          sizeof(uint32_t) * allocationEntryCount,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          sharc_.lockBuffer);
        CreateSharcBuffer(CommandPool(), "SharcAccumulation",
                          sizeof(Assets::SharcAccumulationEntry) * allocationEntryCount,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          sharc_.accumulation);
        CreateSharcBuffer(CommandPool(), "SharcResolved",
                          sizeof(Assets::SharcResolvedEntry) * allocationEntryCount,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          sharc_.resolved);
        CreateSharcBuffer(CommandPool(), "SharcParameters",
                          sizeof(Assets::SharcRuntimeParameters),
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          sharc_.parameters);
        CreateSharcBuffer(CommandPool(), "SharcResources",
                          sizeof(Assets::SharcResources),
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          sharc_.resources);

        Assets::SharcResources resources{};
        resources.HashEntries = sharc_.hashEntries.buffer->GetDeviceAddress();
        resources.LockBuffer = sharc_.lockBuffer.buffer->GetDeviceAddress();
        resources.Accumulation = sharc_.accumulation.buffer->GetDeviceAddress();
        resources.Resolved = sharc_.resolved.buffer->GetDeviceAddress();
        resources.Parameters = sharc_.parameters.buffer->GetDeviceAddress();
        WriteHostVisibleBuffer(sharc_.resources, &resources, sizeof(resources));

        sharc_.pendingClear = true;
        const uint64_t totalBytes = sharc_.hashEntries.size + sharc_.lockBuffer.size + sharc_.accumulation.size +
            sharc_.resolved.size + sharc_.parameters.size + sharc_.resources.size;
        SPDLOG_INFO("SHARC cache allocated: entries=2^{} ({}), memory={:.2f} MiB",
                    entriesPow2,
                    entryCount,
                    static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    }

    void PathTracingRenderer::UpdateSharcParameters()
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(FrameCount(), 0));
        const auto& currentUbo = NextEngine::GetInstance()->GetLastUniformBufferObject();
        const glm::vec4 currentCameraPosition(currentUbo.ModelViewInverse[3][0],
                                              currentUbo.ModelViewInverse[3][1],
                                              currentUbo.ModelViewInverse[3][2],
                                              1.0f);
        const bool frameCounterReset = sharc_.lastFrameIndex != ~0u && frameIndex < sharc_.lastFrameIndex;
        const bool lightingChanged = sharc_.hasLastLightingState &&
            (currentUbo.HasSun != sharc_.lastHasSun ||
             currentUbo.HasSky != sharc_.lastHasSky ||
             currentUbo.SkyIdx != sharc_.lastSkyIdx ||
             currentUbo.SunDirection != sharc_.lastSunDirection ||
             currentUbo.SunColor != sharc_.lastSunColor ||
             currentUbo.SkyIntensity != sharc_.lastSkyIntensity ||
             currentUbo.SkyRotation != sharc_.lastSkyRotation);
        if (frameCounterReset || lightingChanged)
        {
            sharc_.pendingClear = true;
            sharc_.hasLastCameraPosition = false;
        }

        Assets::SharcRuntimeParameters parameters{};
        parameters.EntryCount = sharc_.entryCount;
        parameters.FrameIndex = frameIndex;
        parameters.DebugMode = static_cast<uint32_t>(std::max(settings.SharcDebugMode, 0));
        parameters.SceneScale = std::max(settings.SharcSceneScale, 0.001f);
        parameters.LevelBias = settings.SharcLevelBias;
        parameters.RadianceScale = std::max(settings.SharcRadianceScale, 1.0f);
        parameters.UpdateSampleRatio = std::clamp(settings.SharcUpdateSampleRatio, 0.0f, 1.0f);
        parameters.QueryRoughnessMin = std::clamp(settings.SharcQueryRoughnessMin, 0.0f, 1.0f);
        parameters.QueryMinBounce = settings.SharcQueryMinBounce;
        parameters.AccumulatedFrameMax = std::clamp(settings.SharcAccumulatedFrameMax, 1u, 1024u);
        parameters.ResponsiveFrameMax = std::clamp(settings.SharcResponsiveFrameMax, 1u, 1024u);
        parameters.StaleFrameMax = std::clamp(settings.SharcStaleFrameMax, 8u, 1024u);
        parameters.CameraPositionPrev =
            sharc_.hasLastCameraPosition ? sharc_.lastCameraPosition : currentCameraPosition;
        WriteHostVisibleBuffer(sharc_.parameters, &parameters, sizeof(parameters));

        sharc_.lastFrameIndex = frameIndex;
        sharc_.lastCameraPosition = currentCameraPosition;
        sharc_.hasLastCameraPosition = true;
        sharc_.lastSunDirection = currentUbo.SunDirection;
        sharc_.lastSunColor = currentUbo.SunColor;
        sharc_.lastSkyIdx = currentUbo.SkyIdx;
        sharc_.lastSkyIntensity = currentUbo.SkyIntensity;
        sharc_.lastSkyRotation = currentUbo.SkyRotation;
        sharc_.lastHasSun = currentUbo.HasSun;
        sharc_.lastHasSky = currentUbo.HasSky;
        sharc_.hasLastLightingState = true;
    }

    void PathTracingRenderer::ClearSharcResources(VkCommandBuffer commandBuffer)
    {
        if (!sharc_.pendingClear)
        {
            return;
        }

        vkCmdFillBuffer(commandBuffer, sharc_.hashEntries.buffer->Handle(), 0, sharc_.hashEntries.size, 0);
        vkCmdFillBuffer(commandBuffer, sharc_.lockBuffer.buffer->Handle(), 0, sharc_.lockBuffer.size, 0);
        vkCmdFillBuffer(commandBuffer, sharc_.accumulation.buffer->Handle(), 0, sharc_.accumulation.size, 0);
        vkCmdFillBuffer(commandBuffer, sharc_.resolved.buffer->Handle(), 0, sharc_.resolved.size, 0);

        InsertSharcBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        sharc_.pendingClear = false;
    }

    void PathTracingRenderer::InsertSharcBarrier(
        VkCommandBuffer commandBuffer,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask) const
    {
        if (!sharc_.resources.buffer)
        {
            return;
        }

        std::array<VkBufferMemoryBarrier, 4> barriers{};
        const std::array<const FSharcBuffer*, 4> buffers{
            &sharc_.hashEntries,
            &sharc_.lockBuffer,
            &sharc_.accumulation,
            &sharc_.resolved,
        };
        for (size_t i = 0; i < barriers.size(); ++i)
        {
            barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[i].srcAccessMask = srcAccessMask;
            barriers[i].dstAccessMask = dstAccessMask;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].buffer = buffers[i]->buffer->Handle();
            barriers[i].offset = 0;
            barriers[i].size = VK_WHOLE_SIZE;
        }

        const VkPipelineStageFlags srcStage =
            (srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0 ? VK_PIPELINE_STAGE_TRANSFER_BIT :
                                                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             static_cast<uint32_t>(barriers.size()),
                             barriers.data(),
                             0,
                             nullptr);
    }

    Assets::GPUScene PathTracingRenderer::BuildSharcGPUScene(uint32_t imageIndex)
    {
        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.ReservedAddress0 = sharc_.resources.buffer ? sharc_.resources.buffer->GetDeviceAddress() : 0;
        return gpuScene;
    }

    void PathTracingRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        // Acquire destination images for rendering.
        baseRender_.InitializeBarriers(commandBuffer);
        const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
        const bool offlineProgressiveRender = IsOfflineProgressiveRenderActive();
        const bool sharcEnabled = IsEffectiveSharcEnabled();

        // Execute ray tracing shaders.
        {
            if (sharcEnabled)
            {
                EnsureSharcPipelines();
                EnsureSharcResources();
                UpdateSharcParameters();
                ClearSharcResources(commandBuffer);

                const Assets::GPUScene sharcGpuScene = BuildSharcGPUScene(imageIndex);
                {
                    SCOPED_GPU_TIMER("sharc update pass");
                    sharcUpdatePipeline_->BindPipeline(commandBuffer, sharcGpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                                  Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
                }

                InsertSharcBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

                {
                    SCOPED_GPU_TIMER("sharc resolve pass");
                    sharcResolvePipeline_->BindPipeline(commandBuffer, sharcGpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(sharc_.entryCount, kSharcResolveThreadCount),
                                  1,
                                  1);
                }

                InsertSharcBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

                {
                    SCOPED_GPU_TIMER("sharc query pass");
                    sharcQueryPipeline_->BindPipeline(commandBuffer, sharcGpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                                  Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
                }
            }
            else
            {
                SCOPED_GPU_TIMER("rt pass");
                rayTracingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
                vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                              Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
            }

            baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_NORMAL)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_MOTIONVECTOR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
         // accumulate with reproject
        {
            SCOPED_GPU_TIMER("reproject pass");
            FReprojectPushConstants pushConst{};
            pushConst.ProgressiveRender = NextEngine::GetInstance()->IsProgressiveRendering();
            pushConst.TemporalFrames = offlineProgressiveRender
                ? NextEngine::GetInstance()->GetProgressiveRenderTargetFrames()
                : uint32_t(settings.TemporalFrames);
            pushConst.PrevDiffuseBindlessIdx = temporalResolve_.History(PipelineCommon::ETemporalChannel::Diffuse);
            pushConst.PrevSpecularBindlessIdx = temporalResolve_.History(PipelineCommon::ETemporalChannel::Specular);
            pushConst.PrevAlbedoBindlessIdx = temporalResolve_.History(PipelineCommon::ETemporalChannel::Albedo);
            pushConst.FastReproject = 0;
            pushConst.ClampGammaHi = settings.ReprojectClampGammaHi;
            pushConst.ClampGammaLo = settings.ReprojectClampGammaLo;
            pushConst.ClampFloor = settings.ReprojectClampFloor;
            accumulatePipeline_->BindPipeline(commandBuffer, &pushConst);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL );
            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        // Variance-guided a-trous wavelet on the temporally-accumulated lighting (Phase 2 + 4).
        // The accumulation buffers are left untouched (they remain this frame's history); filtering
        // ping-pongs through shared scratch buffers and lands in a per-channel output, which compose
        // reads via Camera.Denoise*SourceSlot. The diffuse and specular passes run sequentially and
        // share the ping/pong scratch. When disabled, compose falls back to the accumulation buffers.
        {
            if (!offlineProgressiveRender)
            {
                SCOPED_GPU_TIMER("atrous pass");
                atrousDenoiser_.Run(baseRender_, SwapChain(), commandBuffer, settings);
            }
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            composePipelineNonDenoiser_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
        }
        
        {
            SCOPED_GPU_TIMER("copy pass");
            temporalResolve_.CopyToHistory(baseRender_, commandBuffer, {
                {Assets::Bindless::RT_ACCUMLATE_DIFFUSE, PipelineCommon::ETemporalChannel::Diffuse},
                {Assets::Bindless::RT_ACCUMLATE_SPECULAR, PipelineCommon::ETemporalChannel::Specular},
                {Assets::Bindless::RT_ACCUMLATE_ALBEDO, PipelineCommon::ETemporalChannel::Albedo},
            });
        }
    }
}
