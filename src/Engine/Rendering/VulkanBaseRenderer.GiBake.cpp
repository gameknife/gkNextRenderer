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

        const int cubesPerGroup = 64;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t safeCascadeCount = std::max(1u, cascadeCount);
        const uint32_t cascadeIndex = static_cast<uint32_t>(frame_.frameCount % safeCascadeCount);
        const uint32_t activeBrickCount = GetScene().AmbientActiveBrickCount(cascadeIndex);
        if (activeBrickCount == 0u)
        {
            return;
        }
        const int activeProbeCount =
            static_cast<int>(activeBrickCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME));
        const int group = (activeProbeCount + cubesPerGroup - 1) / cubesPerGroup;

        int temporalFrames = 120;
        switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
        {
        case 0: temporalFrames = 30; break;
        case 1: temporalFrames = 120; break;
        case 2: temporalFrames = 300; break;
        default: temporalFrames = 120; break;
        }

        SCOPED_GPU_TIMER(useHardware ? "hw-lightbake" : "sw-lightbake");

        const int frame = static_cast<int>((frame_.frameCount / safeCascadeCount) % temporalFrames);
        const int groupPerFrame = std::max(1, (group + temporalFrames - 1) / temporalFrames);
        const int offset = frame * groupPerFrame;
        if (offset >= group)
        {
            return;
        }

        const int dispatchGroupCount = std::min(groupPerFrame, group - offset);
        const int offsetInActiveProbes = offset * cubesPerGroup;
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
    }

}
