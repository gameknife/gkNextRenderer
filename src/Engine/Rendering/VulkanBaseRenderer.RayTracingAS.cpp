// VulkanBaseRenderer ray tracing acceleration structure lifecycle:
// BLAS/TLAS creation, per-frame updates and teardown.
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
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Exception.hpp"

namespace Vulkan
{
    void VulkanBaseRenderer::UpdateAccelerationStructuresTop(VkCommandBuffer commandBuffer)
    {
        if (!caps_.supportRayTracing)
        {
            return;
        }
        if (!(GOption->ReferenceMode || CurrentRendererRequirements().requestRayTracing ||
              (CurrentRendererRequirements().requestAmbientCube && !GOption->ForceSoftGen)))
        {
            return;
        }
        SCOPED_GPU_TIMER("TLAS Update");
        if (rt_->tlasUpdateRequest > 0)
        {
            rt_->tlas[0].Update(commandBuffer, rt_->tlasUpdateRequest);
            rt_->tlasUpdateRequest = 0;
        }
    }

    void VulkanBaseRenderer::UpdateAccelerationStructuresBottom(VkCommandBuffer commandBuffer)
    {
        if (!caps_.supportRayTracing || !rt_->blasScratch)
        {
            return;
        }
        SCOPED_GPU_TIMER("BLAS Update");
        VkDeviceSize scratchOffset = 0;
        for (size_t i = 0; i < skin_.updateRequests.size(); i++)
        {
            int32_t modelId = skin_.updateRequests[i];
            if (modelId != -1)
            {
                rt_->blas[modelId].Update(commandBuffer, *rt_->blasScratch, scratchOffset);
                scratchOffset += rt_->blas[modelId].BuildSizes().updateScratchSize;
            }
        }
        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);
    }

    std::vector<RayTracing::TopLevelAccelerationStructure>& VulkanBaseRenderer::TLAS()
    {
        static std::vector<RayTracing::TopLevelAccelerationStructure> empty;
        return rt_ ? rt_->tlas : empty;
    }

    VkAccelerationStructureKHR VulkanBaseRenderer::ActiveTLASHandle() const
    {
        if (!rt_ || rt_->tlas.empty())
        {
            return VK_NULL_HANDLE;
        }
        return rt_->tlas.front().Handle();
    }

    namespace
    {
        template <class TAccelerationStructure>
        VkAccelerationStructureBuildSizesInfoKHR GetTotalRequirements(
            const std::vector<TAccelerationStructure>& accelerationStructures)
        {
            VkAccelerationStructureBuildSizesInfoKHR total{};
            for (const auto& accelerationStructure : accelerationStructures)
            {
                total.accelerationStructureSize += accelerationStructure.BuildSizes().accelerationStructureSize;
                total.buildScratchSize += accelerationStructure.BuildSizes().buildScratchSize;
                total.updateScratchSize += accelerationStructure.BuildSizes().updateScratchSize;
            }
            return total;
        }
    }

    void VulkanBaseRenderer::CreateAccelerationStructures()
    {
        const auto timer = std::chrono::high_resolution_clock::now();

        SingleTimeCommands::Submit(CommandPool(), [this](VkCommandBuffer commandBuffer)
        {
            CreateBottomLevelStructures(commandBuffer);
            CreateTopLevelStructures(commandBuffer);
        });

        const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();
        SPDLOG_INFO("- built acceleration structures in {:.2f}ms", elapsed * 1000.f);
    }

    void VulkanBaseRenderer::DeleteAccelerationStructures()
    {
        if (rt_)
        {
            rt_->tlas.clear();
            rt_->instancesBuffer.reset();
            rt_->instancesMemory.reset();
            rt_->tlasScratch.reset();
            rt_->tlasScratchMemory.reset();
            rt_->tlasBuffer.reset();
            rt_->tlasMemory.reset();

            rt_->blas.clear();
            rt_->blasScratch.reset();
            rt_->blasScratchMemory.reset();
            rt_->blasBuffer.reset();
            rt_->blasMemory.reset();
        }
    }

    void VulkanBaseRenderer::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();

        UpdateSkinningBuffers();

        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t aabbOffset = 0;

        if (scene.Models().empty())
        {
            RayTracing::BottomLevelGeometry geometries;
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);
        }

        for (size_t modelIdx = 0; modelIdx < scene.Models().size(); ++modelIdx)
        {
            auto& model = scene.Models()[modelIdx];
            bool hasSkin = false;
            for (const auto& node : scene.Nodes())
            {
                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (render && render->GetModelId() == modelIdx && render->GetSkinIndex() != -1)
                {
                    hasSkin = true;
                    break;
                }
            }

            const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
            const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
            RayTracing::BottomLevelGeometry geometries;

            VkDeviceAddress vertexAddr = 0;
            if (hasSkin && skin_.vertexBuffer)
            {
                vertexAddr = skin_.vertexBuffer->GetDeviceAddress();
            }

            geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true, vertexAddr);
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);

            vertexOffset += vertexCount * sizeof(Assets::GPUVertex);
            indexOffset += indexCount * sizeof(uint32_t);
            aabbOffset += sizeof(VkAabbPositionsKHR);
        }

        const auto total = GetTotalRequirements(rt_->blas);

        rt_->blasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->blasMemory.reset(new DeviceMemory(
            rt_->blasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));
        rt_->blasScratch.reset(new Buffer(Device(), std::max(total.buildScratchSize, total.updateScratchSize),
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->blasScratchMemory.reset(new DeviceMemory(
            rt_->blasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));

        debugUtils.SetObjectName(rt_->blasBuffer->Handle(), "BLAS Buffer");
        rt_->blasMemory->SetName("BLAS Memory");
        debugUtils.SetObjectName(rt_->blasScratch->Handle(), "BLAS Scratch Buffer");
        rt_->blasScratchMemory->SetName("BLAS Scratch Memory");

        VkDeviceSize resultOffset = 0;
        VkDeviceSize scratchOffset = 0;

        for (size_t i = 0; i != rt_->blas.size(); ++i)
        {
            rt_->blas[i].Generate(commandBuffer, *rt_->blasScratch, scratchOffset, *rt_->blasBuffer, resultOffset);
            resultOffset += rt_->blas[i].BuildSizes().accelerationStructureSize;
            scratchOffset += rt_->blas[i].BuildSizes().buildScratchSize;
            debugUtils.SetObjectName(rt_->blas[i].Handle(), ("BLAS #" + std::to_string(i)).c_str());
        }
    }

    void VulkanBaseRenderer::CreateTopLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& debugUtils = Device().DebugUtils();
        const uint64_t requestedCapacity =
            std::max<uint64_t>(1, GetScene().RenderCapacityLimits().renderProxyCapacity);
        const uint64_t deviceLimit = rt_->properties->MaxInstanceCount();
        if (requestedCapacity > deviceLimit)
        {
            Throw(std::runtime_error(fmt::format(
                "TLAS instance requirement {} exceeds device limit {}", requestedCapacity, deviceLimit)));
        }
        const uint32_t instanceCapacity = static_cast<uint32_t>(std::min<uint64_t>(
            std::bit_ceil(requestedCapacity), deviceLimit));
        rt_->tlasInstanceCapacity = instanceCapacity;
        SPDLOG_INFO("TLAS requested/allocated instance capacity: {}/{}",
                    requestedCapacity, instanceCapacity);

        rt_->instancesBuffer.reset(new Buffer(Device(), instanceCapacity * sizeof(VkAccelerationStructureInstanceKHR),
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->instancesMemory.reset(new DeviceMemory(
            rt_->instancesBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);

        rt_->tlas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties,
                            rt_->instancesBuffer->GetDeviceAddress(), instanceCapacity);

        const auto total = GetTotalRequirements(rt_->tlas);

        rt_->tlasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->tlasMemory.reset(new DeviceMemory(
            rt_->tlasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        rt_->tlasScratch.reset(new Buffer(Device(), total.buildScratchSize,
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->tlasScratchMemory.reset(new DeviceMemory(
            rt_->tlasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        debugUtils.SetObjectName(rt_->tlasBuffer->Handle(), "TLAS Buffer");
        rt_->tlasMemory->SetName("TLAS Memory");
        debugUtils.SetObjectName(rt_->tlasScratch->Handle(), "TLAS Scratch Buffer");
        rt_->tlasScratchMemory->SetName("TLAS Scratch Memory");
        debugUtils.SetObjectName(rt_->instancesBuffer->Handle(), "TLAS Instances Buffer");
        rt_->instancesMemory->SetName("TLAS Instances Memory");

        rt_->tlas[0].Generate(commandBuffer, *rt_->tlasScratch, 0, *rt_->tlasBuffer, 0);
        debugUtils.SetObjectName(rt_->tlas[0].Handle(), "TLAS");
    }
}
