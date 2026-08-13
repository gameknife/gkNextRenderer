// VulkanBaseRenderer software GI baking: ambient cube cascade bake and
// distance field cascade rebuild.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"

namespace Vulkan
{
    void VulkanBaseRenderer::HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!ActiveRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
        {
            return;
        }

        if (ambient_.requestClearCache)
        {
            ClearAmbientCubeCache(commandBuffer, imageIndex);
            ambient_.requestClearCache = false;
        }
    }

    bool VulkanBaseRenderer::ShouldSkipAmbientCubeUpdates() const
    {
        if (GOption->ReferenceMode)
        {
            return false;
        }

        return CurrentLogicRendererType() == ERendererType::ERT_PathTracing;
    }

    void VulkanBaseRenderer::ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("clear-ambient-cube-cache");

        constexpr uint32_t cubesPerGroup = 64;
        // Clear only the allocated cascades (Phase 2 right-sizing). The sparse cube pool (Phase 3b) is
        // smaller than the dense voxel array, but VoxelData no longer has transient cache fields.
        const uint32_t clearCascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t poolCubesPerCascade =
            GetScene().AmbientPoolBricksPerCascade() * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
        const uint32_t cubePoolTotal = clearCascadeCount * poolCubesPerCascade;
        const uint32_t residencyTotal =
            clearCascadeCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        const uint32_t groupCount =
            (std::max(cubePoolTotal, residencyTotal) + cubesPerGroup - 1) / cubesPerGroup;

        ambient_.clearCache->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, 0);
        gpuScene.CustomData0 = cubePoolTotal;
        gpuScene.CustomData1 = 0;
        gpuScene.CustomData2 = residencyTotal;

        vkCmdPushConstants(commandBuffer, ambient_.clearCache->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(GetScene().AmbientArenaBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                      GetScene().AmbientCubesByteOffset(), static_cast<VkDeviceSize>(cubePoolTotal) * sizeof(Assets::AmbientCube)),
            BufferMemoryBarrier::Make(GetScene().AmbientArenaBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                      GetScene().AmbientResidencyByteOffset(), static_cast<VkDeviceSize>(residencyTotal) * sizeof(Assets::AmbientBrickResidency)),
        });
    }

    void VulkanBaseRenderer::BakeAmbientCubeCascade(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool useHardware)
    {
        Vulkan::PipelineBase* pipeline = useHardware ? static_cast<Vulkan::PipelineBase*>(rt_->directLightGenPipeline.get())
                                                     : static_cast<Vulkan::PipelineBase*>(ambient_.softBake.get());
        if (!pipeline)
        {
            return;
        }

        constexpr uint32_t cubesPerGroup = 64u;
        constexpr uint32_t convergencePasses = 32u;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        if (cascadeCount == 0u)
        {
            return;
        }

        auto& cpuAcceleration = GetScene().GetCPUAccelerationStructure();
        const uint64_t dirtyRevision = cpuAcceleration.AmbientBakeDirtyRevision();
        if (dirtyRevision == 0u)
        {
            return;
        }

        uint32_t dirtyBrickTotal = 0u;
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
        {
            dirtyBrickTotal += cpuAcceleration.AmbientBakeDirtyBrickCount(cascade);
        }
        if (dirtyBrickTotal == 0u)
        {
            ambient_.dirtyRevision = 0u;
            ambient_.lastDispatchedGroups = 0u;
            return;
        }

        if (ambient_.dirtyRevision != dirtyRevision)
        {
            ambient_.dirtyRevision = dirtyRevision;
            ambient_.nextGroup.fill(0u);
            ambient_.completedPasses.fill(0u);
            ambient_.nextCascade = 0u;
            SPDLOG_INFO("Ambient bake revision {} started: {} dirty bricks, {} tracing",
                        dirtyRevision, dirtyBrickTotal, useHardware ? "hardware" : "software");
        }

        const char* timerName = useHardware ? "hw-lightbake" : "sw-lightbake";
        const float previousMilliseconds = ctx_.frameProfiler->GetGpuTime(timerName);
        if (previousMilliseconds > 0.0f && ambient_.lastDispatchedGroups > 0u)
        {
            const float measuredMillisecondsPerGroup =
                previousMilliseconds / static_cast<float>(ambient_.lastDispatchedGroups);
            ambient_.millisecondsPerGroup = ambient_.millisecondsPerGroup > 0.0f
                ? glm::mix(ambient_.millisecondsPerGroup, measuredMillisecondsPerGroup, 0.2f)
                : measuredMillisecondsPerGroup;
        }

        float targetMilliseconds = 1.0f;
        switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
        {
        case 0: targetMilliseconds = 2.0f; break;
        case 1: targetMilliseconds = 1.0f; break;
        case 2: targetMilliseconds = 0.35f; break;
        default: break;
        }
        if (ambient_.millisecondsPerGroup > 0.0f)
        {
            const uint32_t measuredBudget = std::max(
                1u, static_cast<uint32_t>(targetMilliseconds / ambient_.millisecondsPerGroup));
            // Limit the controller slew rate so one noisy timestamp cannot cause a large frame spike.
            const uint32_t lower = std::max(1u, ambient_.groupsPerFrame / 2u);
            const uint32_t upper = std::max(2u, ambient_.groupsPerFrame * 2u);
            ambient_.groupsPerFrame = std::clamp(measuredBudget, lower, upper);
        }

        uint32_t cascadeIndex = cascadeCount;
        for (uint32_t attempt = 0; attempt < cascadeCount; ++attempt)
        {
            const uint32_t candidate = (ambient_.nextCascade + attempt) % cascadeCount;
            if (cpuAcceleration.AmbientBakeDirtyBrickCount(candidate) > 0u &&
                ambient_.completedPasses[candidate] < convergencePasses)
            {
                cascadeIndex = candidate;
                ambient_.nextCascade = (candidate + 1u) % cascadeCount;
                break;
            }
        }

        if (cascadeIndex == cascadeCount)
        {
            cpuAcceleration.AcknowledgeAmbientBake(dirtyRevision);
            SPDLOG_INFO("Ambient bake revision {} converged after {} passes; scheduler idle",
                        dirtyRevision, convergencePasses);
            ambient_.dirtyRevision = 0u;
            ambient_.lastDispatchedGroups = 0u;
            return;
        }

        const uint32_t dirtyBrickCount = cpuAcceleration.AmbientBakeDirtyBrickCount(cascadeIndex);
        const uint32_t dirtyProbeCount =
            dirtyBrickCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
        const uint32_t totalGroups = (dirtyProbeCount + cubesPerGroup - 1u) / cubesPerGroup;
        if (totalGroups == 0u)
        {
            return;
        }

        const uint32_t offset = std::min(ambient_.nextGroup[cascadeIndex], totalGroups);
        const uint32_t dispatchGroupCount = std::min(ambient_.groupsPerFrame, totalGroups - offset);
        const uint32_t offsetInActiveProbes = offset * cubesPerGroup;

        SCOPED_GPU_TIMER(timerName);

        if (dispatchGroupCount == 0u)
        {
            return;
        }
        const VkBuffer cubeBuffer = GetScene().AmbientArenaBuffer().Handle();
        const VkBuffer pongBuffer = cubeBuffer;
        // The cube pool is laid out per cascade with poolCubesPerCascade cubes; the ping-pong copy and
        // its barriers operate on that pool stride, not the dense per-cascade probe count.
        const VkDeviceSize poolCubesPerCascade =
            static_cast<VkDeviceSize>(GetScene().AmbientPoolBricksPerCascade()) * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;
        const VkDeviceSize cascadeByteOffset =
            GetScene().AmbientCubesByteOffset() +
            static_cast<VkDeviceSize>(cascadeIndex) * poolCubesPerCascade * sizeof(Assets::AmbientCube);
        const VkDeviceSize pongByteOffset = GetScene().AmbientCubesPongByteOffset();
        const VkDeviceSize cascadeByteSize = poolCubesPerCascade * sizeof(Assets::AmbientCube);

        // ping (cube) -> pong copy with surrounding barriers
        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    cubeBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, cascadeByteOffset, cascadeByteSize);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = cascadeByteOffset;
        copyRegion.dstOffset = pongByteOffset;
        copyRegion.size = cascadeByteSize;
        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(pongBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, pongByteOffset, cascadeByteSize),
            BufferMemoryBarrier::Make(cubeBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, cascadeByteOffset, cascadeByteSize),
        });

        // Dispatch the chosen bake pipeline. Both share the same GPUScene push constant layout.
        if (useHardware)
        {
            rt_->directLightGenPipeline->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }
        else
        {
            ambient_.softBake->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, 0);
        gpuScene.CustomData0 = static_cast<uint32_t>(offsetInActiveProbes);
        gpuScene.CustomData1 = cascadeIndex;

        vkCmdPushConstants(commandBuffer, pipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);

        ambient_.lastDispatchedGroups = dispatchGroupCount;
        ambient_.nextGroup[cascadeIndex] = offset + dispatchGroupCount;
        if (ambient_.nextGroup[cascadeIndex] >= totalGroups)
        {
            ambient_.nextGroup[cascadeIndex] = 0u;
            ++ambient_.completedPasses[cascadeIndex];
        }
    }

}
