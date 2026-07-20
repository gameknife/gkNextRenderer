#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Utilities/Math.hpp"
#include <numeric>

namespace Vulkan::PathTracing
{
    namespace
    {
        constexpr uint32_t kSharcResolveThreadCount = 64;
        constexpr uint32_t kSharcMinEntriesPow2 = 10;
        constexpr uint32_t kSharcMaxEntriesPow2 = 22;
        constexpr uint32_t kSharcProbePadding = 16;

        static_assert(sizeof(Assets::SharcHashEntry) == 8);
        static_assert(sizeof(Assets::SharcAccumulationEntry) == 16);
        static_assert(sizeof(Assets::SharcResolvedEntry) == 16);
        static_assert(sizeof(Assets::SharcRuntimeParameters) == 64);
        static_assert(sizeof(Assets::FPathTracingExtras) == 64);
        static_assert(sizeof(Assets::FRestirReservoir) == 16);
        static_assert(sizeof(Assets::FRestirRuntimeParameters) == 48);

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
        rayTracingPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
            SwapChain(), "assets/shaders/Core.PathTracing.comp.slang.spv", GetScene(), baseRender_.ActiveTLASHandle()));
        temporalPostChain_.CreateSwapChain(SwapChain(), GetScene());
    }

    void PathTracingRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        sharcUpdatePipeline_.reset();
        sharcResolvePipeline_.reset();
        sharcQueryPipeline_.reset();
        restirSpatialPipeline_.reset();
        temporalPostChain_.DeleteSwapChain();
    }

    void PathTracingRenderer::EnsureSharcPipelines()
    {
        if (!sharcUpdatePipeline_)
        {
            sharcUpdatePipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                SwapChain(), "assets/shaders/Core.SharcUpdate.comp.slang.spv", GetScene(), baseRender_.ActiveTLASHandle()));
        }
        if (!sharcResolvePipeline_)
        {
            sharcResolvePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
                SwapChain(), "assets/shaders/Core.SharcResolve.comp.slang.spv", GetScene()));
        }
        if (!sharcQueryPipeline_)
        {
            sharcQueryPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                SwapChain(), "assets/shaders/Core.SharcQuery.comp.slang.spv", GetScene(), baseRender_.ActiveTLASHandle()));
        }
    }

    bool PathTracingRenderer::IsOfflineProgressiveRenderActive() const
    {
        return baseRender_.FrameSettings().offlineProgressivePathTracing;
    }

    bool PathTracingRenderer::IsEffectiveSharcEnabled() const
    {
        return baseRender_.FrameSettings().effectiveSharc;
    }

    void PathTracingRenderer::EnsureSharcResources()
    {
        const Assets::Scene* activeScene = &GetScene();
        const uint64_t sceneGeneration = baseRender_.SceneGeneration();
        if (sharc_.ownerScene != activeScene || sharc_.ownerSceneGeneration != sceneGeneration)
        {
            sharc_ = {};
            sharc_.ownerScene = activeScene;
            sharc_.ownerSceneGeneration = sceneGeneration;
        }
        const auto& settings = baseRender_.FrameSettings().userSettings;
        const uint32_t entriesPow2 = std::clamp(settings.SharcEntriesPow2, kSharcMinEntriesPow2, kSharcMaxEntriesPow2);
        const uint32_t entryCount = 1u << entriesPow2;
        if (sharc_.entryCount == entryCount && sharc_.hashEntries.buffer)
        {
            return;
        }

        const Assets::Scene* ownerScene = sharc_.ownerScene;
        const uint64_t ownerSceneGeneration = sharc_.ownerSceneGeneration;
        sharc_ = {};
        sharc_.ownerScene = ownerScene;
        sharc_.ownerSceneGeneration = ownerSceneGeneration;
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

        sharc_.pendingClear = true;
        const uint64_t totalBytes = sharc_.hashEntries.size + sharc_.lockBuffer.size + sharc_.accumulation.size +
            sharc_.resolved.size + sharc_.parameters.size;
        SPDLOG_INFO("SHARC cache allocated: entries=2^{} ({}), memory={:.2f} MiB",
                    entriesPow2,
                    entryCount,
                    static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    }

    void PathTracingRenderer::UpdateSharcParameters()
    {
        const auto& settings = baseRender_.FrameSettings().userSettings;
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(FrameCount(), 0));
        const auto& currentUbo = baseRender_.ActiveRenderView().State().previousUniformBuffer;
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
        if (!sharc_.hashEntries.buffer)
        {
            return;
        }

        const VkPipelineStageFlags srcStage =
            (srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0 ? VK_PIPELINE_STAGE_TRANSFER_BIT :
                                                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        BufferMemoryBarrier::Insert(commandBuffer, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(sharc_.hashEntries.buffer->Handle(), srcAccessMask, dstAccessMask),
            BufferMemoryBarrier::Make(sharc_.lockBuffer.buffer->Handle(), srcAccessMask, dstAccessMask),
            BufferMemoryBarrier::Make(sharc_.accumulation.buffer->Handle(), srcAccessMask, dstAccessMask),
            BufferMemoryBarrier::Make(sharc_.resolved.buffer->Handle(), srcAccessMask, dstAccessMask),
        });
    }

    bool PathTracingRenderer::IsRestirEnabled() const
    {
        return baseRender_.FrameSettings().userSettings.RestirEnable;
    }

    void PathTracingRenderer::EnsureRestirResources(const VkExtent2D& extent)
    {
        if (restir_.reservoirPing.buffer &&
            restir_.extent.width == extent.width && restir_.extent.height == extent.height)
        {
            return;
        }

        restir_ = {};
        restir_.extent = extent;
        const VkDeviceSize reservoirBytes =
            sizeof(Assets::FRestirReservoir) * VkDeviceSize(extent.width) * VkDeviceSize(extent.height);
        CreateSharcBuffer(CommandPool(), "RestirReservoirPing", reservoirBytes,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, restir_.reservoirPing);
        CreateSharcBuffer(CommandPool(), "RestirReservoirPong", reservoirBytes,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, restir_.reservoirPong);
        CreateSharcBuffer(CommandPool(), "RestirParameters",
                          sizeof(Assets::FRestirRuntimeParameters),
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          restir_.parameters);
        restir_.pendingClear = true;
        SPDLOG_INFO("ReSTIR reservoirs allocated: {}x{}, memory={:.2f} MiB", extent.width, extent.height,
                    static_cast<double>(reservoirBytes * 2) / (1024.0 * 1024.0));
    }

    void PathTracingRenderer::UpdateRestirParameters()
    {
        const auto& settings = baseRender_.FrameSettings().userSettings;
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(FrameCount(), 0));
        const uint64_t lightsGeneration = GetScene().LightsGeneration();
        // Reservoir history follows the TemporalResolve validity rules (camera cut, scene /
        // renderer switch, non-consecutive frames) plus ReSTIR-specific invalidation: the
        // reservoirs themselves must be continuous (no recreation gap) and the light set
        // must not have been re-ordered, or stored light indices change meaning.
        const bool temporalValid =
            baseRender_.ActiveRenderView().TemporalResolve().IsHistoryValidForFrame(static_cast<int>(frameIndex)) &&
            restir_.lastFrameIndex != ~0u && frameIndex == restir_.lastFrameIndex + 1 &&
            lightsGeneration == restir_.lastLightsGeneration &&
            !restir_.pendingClear;
        restir_.lastFrameIndex = frameIndex;
        restir_.lastLightsGeneration = lightsGeneration;
        // Bit0 (frame parity) is unused since the reservoirs moved to fixed roles
        // (intermediate / final); only the temporal-valid bit remains.
        restir_.frameStamp = temporalValid ? 0x2u : 0u;

        // Offline progressive rendering must converge to ground truth: reuse is disabled so
        // ReSTIR degrades to unbiased RIS-only (design §3.5), which doubles as the built-in
        // bias control arm.
        const bool reuseAllowed = !IsOfflineProgressiveRenderActive();

        Assets::FRestirRuntimeParameters params{};
        params.FrameIndex = frameIndex;
        params.DebugMode = static_cast<uint32_t>(std::max(settings.RestirDebugMode, 0));
        params.Flags = (settings.RestirTemporal && reuseAllowed ? 0x1u : 0u) |
                       (settings.RestirSpatial && reuseAllowed ? 0x2u : 0u) |
                       (temporalValid ? 0x4u : 0u);
        params.LightsGeneration = static_cast<uint32_t>(lightsGeneration);
        params.ReservoirWidth = restir_.extent.width;
        params.ReservoirHeight = restir_.extent.height;
        params.InitialCandidates = std::clamp(settings.RestirCandidates, 1u, 64u);
        params.TemporalMClamp = std::max(settings.RestirMClamp, 1u);
        params.SpatialRadius = std::max(settings.RestirSpatialRadius, 1.0f);
        params.SpatialSamples = std::clamp(settings.RestirSpatialSamples, 1u, 16u);
        WriteHostVisibleBuffer(restir_.parameters, &params, sizeof(params));
    }

    void PathTracingRenderer::ClearRestirResources(VkCommandBuffer commandBuffer)
    {
        if (!restir_.pendingClear)
        {
            return;
        }

        vkCmdFillBuffer(commandBuffer, restir_.reservoirPing.buffer->Handle(), 0, restir_.reservoirPing.size, 0);
        vkCmdFillBuffer(commandBuffer, restir_.reservoirPong.buffer->Handle(), 0, restir_.reservoirPong.size, 0);
        InsertRestirBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        restir_.pendingClear = false;
    }

    void PathTracingRenderer::InsertRestirBarrier(
        VkCommandBuffer commandBuffer,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask) const
    {
        if (!restir_.reservoirPing.buffer)
        {
            return;
        }

        const VkPipelineStageFlags srcStage =
            (srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0 ? VK_PIPELINE_STAGE_TRANSFER_BIT :
                                                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        BufferMemoryBarrier::Insert(commandBuffer, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(restir_.reservoirPing.buffer->Handle(), srcAccessMask, dstAccessMask),
            BufferMemoryBarrier::Make(restir_.reservoirPong.buffer->Handle(), srcAccessMask, dstAccessMask),
        });
    }

    void PathTracingRenderer::UpdateExtrasTable(bool sharcActive)
    {
        if (!extras_.buffer)
        {
            CreateSharcBuffer(CommandPool(), "PathTracingExtras",
                              sizeof(Assets::FPathTracingExtras),
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              extras_);
        }

        // Content must stay view-invariant: every view's Render() reaches this host-visible
        // table, so any view-dependent field would race across views within a frame. The
        // buffer is only rewritten when an address actually changes (buffer recreation):
        // in-flight frames read this memory, so a steady-state per-frame memcpy would be a
        // torn-read hazard against the GPU.
        Assets::FPathTracingExtras extras{};
        if (sharcActive && sharc_.hashEntries.buffer)
        {
            extras.HashEntries = sharc_.hashEntries.buffer->GetDeviceAddress();
            extras.LockBuffer = sharc_.lockBuffer.buffer->GetDeviceAddress();
            extras.Accumulation = sharc_.accumulation.buffer->GetDeviceAddress();
            extras.Resolved = sharc_.resolved.buffer->GetDeviceAddress();
            extras.Parameters = sharc_.parameters.buffer->GetDeviceAddress();
        }
        if (IsRestirEnabled() && restir_.reservoirPing.buffer)
        {
            extras.RestirReservoirPing = restir_.reservoirPing.buffer->GetDeviceAddress();
            extras.RestirReservoirPong = restir_.reservoirPong.buffer->GetDeviceAddress();
            extras.RestirParameters = restir_.parameters.buffer->GetDeviceAddress();
        }
        if (std::memcmp(&extras, &lastExtrasContent_, sizeof(extras)) != 0)
        {
            WriteHostVisibleBuffer(extras_, &extras, sizeof(extras));
            lastExtrasContent_ = extras;
        }
    }

    void PathTracingRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        baseRender_.ActiveRenderView().TemporalResolve().PrepareHistoryForRead(baseRender_, commandBuffer);
        const FFrameRenderSettings& frameSettings = baseRender_.FrameSettings();
        const Runtime::Config::UserSettings& settings = frameSettings.userSettings;
        const bool offlineProgressiveRender = IsOfflineProgressiveRenderActive();
        const bool sharcEnabled = IsEffectiveSharcEnabled();
        const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
        const bool allowTemporal = baseRender_.ActiveRenderView().Schedule() != EViewSchedule::Transient;
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

        // Execute ray tracing shaders.
        {
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_DIFFUSE_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SPECULAR_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_SPECULAR_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_MOTIONMOMENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            }, "path tracing shading");
            // ReSTIR reservoirs are primary-view state; non-primary views skip ReSTIR shader-side
            // via the recorded view bank base (gpuScene.CustomData0 != 0).
            const bool restirEnabled = IsRestirEnabled();
            if (restirEnabled && isPrimaryView)
            {
                EnsureRestirResources(activeExtent);
                UpdateRestirParameters();
                ClearRestirResources(commandBuffer);
            }

            Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
            if (sharcEnabled || restirEnabled)
            {
                if (sharcEnabled)
                {
                    EnsureSharcPipelines();
                    EnsureSharcResources();
                    UpdateSharcParameters();
                    ClearSharcResources(commandBuffer);
                }
                UpdateExtrasTable(sharcEnabled);
                gpuScene.ReservedAddress0 = extras_.buffer->GetDeviceAddress();
                // ReSTIR frame stamp (bit0 parity, bit1 temporal valid) rides the recorded
                // push constant; the host-visible parameter buffer races with in-flight frames.
                if (restirEnabled && isPrimaryView)
                {
                    gpuScene.CustomData1 = restir_.frameStamp;
                }
            }

            if (sharcEnabled)
            {
                {
                    SCOPED_GPU_TIMER("sharc update pass");
                    sharcUpdatePipeline_->BindPipeline(commandBuffer, gpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                                  Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
                }

                InsertSharcBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

                {
                    SCOPED_GPU_TIMER("sharc resolve pass");
                    sharcResolvePipeline_->BindPipeline(commandBuffer, gpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(sharc_.entryCount, kSharcResolveThreadCount),
                                  1,
                                  1);
                }

                InsertSharcBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

                {
                    SCOPED_GPU_TIMER("sharc query pass");
                    sharcQueryPipeline_->BindPipeline(commandBuffer, gpuScene);
                    vkCmdDispatch(commandBuffer,
                                  Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                                  Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
                }
            }
            else
            {
                SCOPED_GPU_TIMER("rt pass");
                rayTracingPipeline_->BindPipeline(commandBuffer, gpuScene);
                vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
            }

            // ReSTIR pass 2: spatial reuse + final visibility + shading. Consumes the
            // intermediate reservoirs and G-buffer written above, adds the area-light direct
            // term into RT_SINGLE_DIFFUSE and stores the final reservoirs for the next
            // frame's temporal merge. Runs whenever ReSTIR is on (spatial toggle only
            // controls the neighbor count inside the shader).
            if (restirEnabled && isPrimaryView && restir_.reservoirPing.buffer)
            {
                if (!restirSpatialPipeline_)
                {
                    restirSpatialPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                        SwapChain(), "assets/shaders/Core.RestirSpatialShade.comp.slang.spv", GetScene(),
                        baseRender_.ActiveTLASHandle()));
                }

                InsertRestirBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                baseRender_.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                    {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
                }, "restir spatial shade");

                SCOPED_GPU_TIMER("restir spatial shade");
                restirSpatialPipeline_->BindPipeline(commandBuffer, gpuScene);
                vkCmdDispatch(commandBuffer,
                              Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
            }
        }
        
        temporalPostChain_.Run(baseRender_, SwapChain(), commandBuffer, imageIndex, settings, {
            .progressiveRender = isPrimaryView && frameSettings.progressiveRendering,
            .fastReproject = false,
            .runAtrous = !offlineProgressiveRender,
            .temporalFrames = isPrimaryView && offlineProgressiveRender
                ? frameSettings.progressiveTargetFrames
                : (allowTemporal ? uint32_t(settings.TemporalFrames) : 1u),
        });
    }
}
