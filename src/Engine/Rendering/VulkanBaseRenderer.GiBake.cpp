// VulkanBaseRenderer software GI baking: ambient cube cascade bake and
// distance field cascade rebuild.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
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

namespace Vulkan
{
    void VulkanBaseRenderer::HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!CurrentRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
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
        return CurrentLogicRendererType() == ERendererType::ERT_PathTracing &&
            NextEngine::GetInstance()->IsEffectiveSharcEnabled();
    }

    void VulkanBaseRenderer::ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("clear-ambient-cube-cache");

        constexpr uint32_t cubesPerGroup = 64;
        constexpr uint32_t perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        // Clear only the allocated cascades (Phase 2 right-sizing). The sparse cube pool (Phase 3b) is
        // smaller than the dense voxel array, so cubes and voxel ages are cleared over separate counts.
        const uint32_t clearCascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t poolCubesPerCascade =
            GetScene().AmbientPoolBricksPerCascade() * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
        const uint32_t cubePoolTotal = clearCascadeCount * poolCubesPerCascade;
        const uint32_t voxelDenseTotal = clearCascadeCount * perCascadeCount;
        const uint32_t residencyTotal =
            clearCascadeCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        const uint32_t groupCount =
            (std::max({cubePoolTotal, voxelDenseTotal, residencyTotal}) + cubesPerGroup - 1) / cubesPerGroup;

        ambient_.clearCache->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = cubePoolTotal;
        gpuScene.custom_data_1 = voxelDenseTotal;
        gpuScene.custom_data_2 = residencyTotal;

        vkCmdPushConstants(commandBuffer, ambient_.clearCache->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        VkBufferMemoryBarrier barriers[3]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = GetScene().AmbientCubeBuffer().Handle();
        barriers[0].offset = GetScene().AmbientCubesByteOffset();
        barriers[0].size = static_cast<VkDeviceSize>(cubePoolTotal) * sizeof(Assets::AmbientCube);

        barriers[1] = barriers[0];
        barriers[1].buffer = GetScene().FarAmbientCubeBuffer().Handle();
        barriers[1].offset = GetScene().AmbientVoxelsByteOffset();
        barriers[1].size = static_cast<VkDeviceSize>(voxelDenseTotal) * sizeof(Assets::VoxelData);

        barriers[2] = barriers[0];
        barriers[2].buffer = GetScene().AmbientCubeBuffer().Handle();
        barriers[2].offset = GetScene().AmbientResidencyByteOffset();
        barriers[2].size = static_cast<VkDeviceSize>(residencyTotal) * sizeof(Assets::AmbientBrickResidency);

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 3, barriers, 0, nullptr);
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
        VkBuffer cubeBuffer = GetScene().AmbientCubeBuffer().Handle();
        VkBuffer pongBuffer = GetScene().AmbientCubePongBuffer().Handle();
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
        VkBufferMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        preCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.buffer = cubeBuffer;
        preCopyBarrier.offset = cascadeByteOffset;
        preCopyBarrier.size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &preCopyBarrier, 0, nullptr);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = cascadeByteOffset;
        copyRegion.dstOffset = pongByteOffset;
        copyRegion.size = cascadeByteSize;
        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

        VkBufferMemoryBarrier postCopyBarriers[2]{};
        postCopyBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postCopyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[0].buffer = pongBuffer;
        postCopyBarriers[0].offset = pongByteOffset;
        postCopyBarriers[0].size = cascadeByteSize;
        postCopyBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        postCopyBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        postCopyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarriers[1].buffer = cubeBuffer;
        postCopyBarriers[1].offset = cascadeByteOffset;
        postCopyBarriers[1].size = cascadeByteSize;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 2, postCopyBarriers, 0, nullptr);

        // Dispatch the chosen bake pipeline. Both share the same GPUScene push constant layout.
        if (useHardware)
        {
            rt_->directLightGenPipeline->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }
        else
        {
            ambient_.softBake->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = static_cast<uint32_t>(offsetInActiveProbes);
        gpuScene.custom_data_1 = cascadeIndex;

        vkCmdPushConstants(commandBuffer, pipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);
    }

    void VulkanBaseRenderer::BakeVoxelSkyVisibility(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!ambient_.voxelSkyVisBake)
        {
            return;
        }

        constexpr int voxelsPerGroup = 64;
        constexpr int perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const int group = perCascadeCount / voxelsPerGroup;

        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t safeCascadeCount = std::max(1u, cascadeCount);
        const uint32_t cascadeIndex = static_cast<uint32_t>(frame_.frameCount % safeCascadeCount);

        // Temporally slice the dense voxel grid across frames (same cadence as the cube bake) so a
        // single frame never sweeps all 1.77M voxels per cascade.
        int temporalFrames = 120;
        switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
        {
        case 0: temporalFrames = 30; break;
        case 1: temporalFrames = 120; break;
        case 2: temporalFrames = 300; break;
        default: temporalFrames = 120; break;
        }

        SCOPED_GPU_TIMER("voxel skyvis bake");

        const int frame = static_cast<int>((frame_.frameCount / safeCascadeCount) % temporalFrames);
        const int groupPerFrame = std::max(1, (group + temporalFrames - 1) / temporalFrames);
        const int offset = frame * groupPerFrame;
        if (offset >= group)
        {
            return;
        }
        const int dispatchGroupCount = std::min(groupPerFrame, group - offset);
        const uint32_t localVoxelBase = static_cast<uint32_t>(offset * voxelsPerGroup);

        ambient_.voxelSkyVisBake->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
        gpuScene.custom_data_0 = localVoxelBase;
        gpuScene.custom_data_1 = cascadeIndex;
        gpuScene.custom_data_2 = 0;
        vkCmdPushConstants(commandBuffer, ambient_.voxelSkyVisBake->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);

        // The bake writes VoxelData.age (sky-vis high byte). Make it visible to next frame's shading
        // pass which trilinearly samples the voxel sky-visibility.
        const VkDeviceSize cascadeByteOffset =
            GetScene().AmbientVoxelsByteOffset() +
            static_cast<VkDeviceSize>(cascadeIndex) * perCascadeCount * sizeof(Assets::VoxelData);
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = GetScene().FarAmbientCubeBuffer().Handle();
        barrier.offset = cascadeByteOffset;
        barrier.size = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(Assets::VoxelData);
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

}
