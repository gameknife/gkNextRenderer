#include "RayTraceBaseRenderer.hpp"
#include "Common/CoreMinimal.hpp"
#include "Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Assets/Core/Model.hpp"
#include "Assets/Core/Scene.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Vulkan/GpuResources.hpp"
#include "Vulkan/RenderingPipeline.hpp"
#include "Vulkan/CommandExecution.hpp"
#include <chrono>
#include <numeric>
#include <spdlog/spdlog.h>
#include <utility>

#include "Runtime/Engine.hpp"


namespace Vulkan::RayTracing
{
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
    
    RayTraceBaseRenderer::RayTraceBaseRenderer(Vulkan::Window* window, const VkPresentModeKHR presentMode,
                                               const bool enableValidationLayers, Instance* instance) :
        Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers, instance)
    {

    }

    RayTraceBaseRenderer::~RayTraceBaseRenderer()
    {
        RayTraceBaseRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        rayTracingProperties_.reset();
    }

    void RayTraceBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        // Required extensions.
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                                      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                      VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                  });

        // Required device features.
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
        accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelerationStructureFeatures.pNext = nextDeviceFeatures;
        accelerationStructureFeatures.accelerationStructure = true;

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.pNext = &accelerationStructureFeatures;
        rayQueryFeatures.rayQuery = true;

        Vulkan::VulkanBaseRenderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &rayQueryFeatures);
    }

    void RayTraceBaseRenderer::OnDeviceSet()
    {
        rayTracingProperties_.reset(new RayTracingProperties(Device()));
        Vulkan::VulkanBaseRenderer::OnDeviceSet();
    }

    void RayTraceBaseRenderer::CreateAccelerationStructures()
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

    void RayTraceBaseRenderer::DeleteAccelerationStructures()
    {
        topAs_.clear();
        instancesBuffer_.reset();
        instancesBufferMemory_.reset();
        topScratchBuffer_.reset();
        topScratchBufferMemory_.reset();
        topBuffer_.reset();
        topBufferMemory_.reset();

        bottomAs_.clear();
        bottomScratchBuffer_.reset();
        bottomScratchBufferMemory_.reset();
        bottomBuffer_.reset();
        bottomBufferMemory_.reset();
    }

    void RayTraceBaseRenderer::CreateSwapChain()
    {
        Vulkan::VulkanBaseRenderer::CreateSwapChain();
        directLightGenPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline(SwapChain(), "assets/shaders/Bake.HwAmbientCube.comp.slang.spv"));
    }

    void RayTraceBaseRenderer::DeleteSwapChain()
    {
        directLightGenPipeline_.reset();
        Vulkan::VulkanBaseRenderer::DeleteSwapChain();
    }

    void RayTraceBaseRenderer::AfterRenderCmd()
    {
        VulkanBaseRenderer::AfterRenderCmd();
    }

    void RayTraceBaseRenderer::BeforeNextFrame()
    {
        VulkanBaseRenderer::BeforeNextFrame();
    }

    void RayTraceBaseRenderer::AfterUpdateScene()
    {
        VulkanBaseRenderer::AfterUpdateScene();

        auto& scene = GetScene();

        // rebuild all instance
        std::vector<VkAccelerationStructureInstanceKHR> instances;
        auto& nodeTrans = scene.GetNodeProxys();
        for ( size_t i = 0; i < nodeTrans.size(); i++)
        {
            auto& node = nodeTrans[i];
            instances.push_back(TopLevelAccelerationStructure::CreateInstance(
                bottomAs_[node.modelId / 10], glm::transpose(node.worldTS), node.instanceId, node.visible && !node.nort));
        }

        // upload to gpu
        int instanceCount = static_cast<int>(instances.size());
        if(instanceCount  >  0)
        {
            VkAccelerationStructureInstanceKHR* data = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(instancesBufferMemory_->Map(0, instances.size() * sizeof(VkAccelerationStructureInstanceKHR)));
            std::memcpy(data, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
            instancesBufferMemory_->Unmap();
        }

        tlasUpdateRequest_ = instanceCount;
    }
    
    void RayTraceBaseRenderer::OnPreLoadScene()
    {
        Vulkan::VulkanBaseRenderer::OnPreLoadScene();
        DeleteAccelerationStructures();
    }

    void RayTraceBaseRenderer::OnPostLoadScene()
    {
        Vulkan::VulkanBaseRenderer::OnPostLoadScene();
        CreateAccelerationStructures();
    }

    void RayTraceBaseRenderer::PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if ( GOption->ReferenceMode || !GOption->ForceSoftGen || CurrentLogicRendererType() == ERT_PathTracing )
        {
            SCOPED_GPU_TIMER("TLAS Update");
            if (tlasUpdateRequest_ > 0)
            {
                topAs_[0].Update(commandBuffer, tlasUpdateRequest_);
                tlasUpdateRequest_ = 0;
            }
        }

        VulkanBaseRenderer::PreRender(commandBuffer, imageIndex);

        if (bottomScratchBuffer_)
        {
            SCOPED_GPU_TIMER("BLAS Update");
            VkDeviceSize scratchOffset = 0;
            for ( size_t i = 0; i < skinModelUpdateRequests_.size(); i++)
            {
                int32_t modelId = skinModelUpdateRequests_[i];
                if (modelId != -1)
                {
                    bottomAs_[modelId].Update(commandBuffer, *bottomScratchBuffer_, scratchOffset);
                }
                scratchOffset += bottomAs_[modelId].BuildSizes().buildScratchSize;
            }
            AccelerationStructure::InsertMemoryBarrier(commandBuffer);
        }
    }

    void RayTraceBaseRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        VulkanBaseRenderer::Render(commandBuffer, imageIndex);
    }

    void RayTraceBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        VulkanBaseRenderer::PostRender(commandBuffer, imageIndex);
        
        if(supportRayTracing_ && !GOption->ForceSoftGen)
        {
            const int cubesPerGroup = 64;
            const int perCascadeCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
            const int group = perCascadeCount / cubesPerGroup;
            const uint32_t cascadeCount = Assets::SanitizeAmbientCubeCascadeCount(
                NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount);

            // 每32个cube一个group
            int temporalFrames = 120;
            switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
            {
            case 0:
                temporalFrames = 30;
                break;
            case 1:
                temporalFrames = 120;
                break;
            case 2:
                temporalFrames = 300;
                break;
            default:
                temporalFrames = 120;
                break;
            }

            {
                SCOPED_GPU_TIMER("hw-lightbake");
                if (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel != 2)
                {
                    int frame = (int)(frameCount_ % temporalFrames);
                    const uint32_t safeCascadeCount = std::max(1u, cascadeCount);
                    frame = static_cast<int>((frameCount_ / safeCascadeCount) % temporalFrames);
                    const int groupPerFrame = std::max(1, (group + temporalFrames - 1) / temporalFrames);
                    const int offset = frame * groupPerFrame;
                    if (offset < group)
                    {
                        const int dispatchGroupCount = std::min(groupPerFrame, group - offset);
                        int offsetInCubes = offset * cubesPerGroup;
                        const uint32_t cascadeIndex = static_cast<uint32_t>(frameCount_ % safeCascadeCount);
                        const uint32_t cascadeBaseOffset = cascadeIndex * static_cast<uint32_t>(perCascadeCount);
                        VkBuffer cubeBuffer = GetScene().AmbientCubeBuffer().Handle();
                        VkBuffer pongBuffer = GetScene().AmbientCubePongBuffer().Handle();
                        const VkDeviceSize cascadeByteOffset = static_cast<VkDeviceSize>(cascadeBaseOffset) * sizeof(Assets::AmbientCube);
                        const VkDeviceSize cascadeByteSize = static_cast<VkDeviceSize>(perCascadeCount) * sizeof(Assets::AmbientCube);

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
                        copyRegion.dstOffset = 0;
                        copyRegion.size = cascadeByteSize;
                        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

                        VkBufferMemoryBarrier postCopyBarriers[2]{};
                        postCopyBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        postCopyBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        postCopyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        postCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[0].buffer = pongBuffer;
                        postCopyBarriers[0].offset = 0;
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
                    
                        directLightGenPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);

                        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex);
                        gpuScene.custom_data_0 = cascadeBaseOffset + offsetInCubes;
                        gpuScene.custom_data_1 = cascadeIndex;
                        gpuScene.custom_data_2 = NextEngine::GetInstance()->GetUserSettings().UseAmbientCubePropagation ? 1u : 0u;
                        
                        vkCmdPushConstants(commandBuffer, directLightGenPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, sizeof(Assets::GPUScene), &gpuScene);
                
                        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);
                    }
                }
            }

            if (NextEngine::GetInstance()->GetUserSettings().UseAmbientCubePropagation &&
                NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel != 2)
            {
                BakeAmbientCubePropagation(commandBuffer, imageIndex);
            }
        }
    }

    void RayTraceBaseRenderer::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();

        UpdateSkinningBuffers();

        // Bottom level acceleration structure
        // Triangles via vertex buffers. Procedurals via AABBs.
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t aabbOffset = 0;

        if(scene.Models().empty())
        {
            BottomLevelGeometry geometries;
            bottomAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, geometries);
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
            BottomLevelGeometry geometries;

            VkDeviceAddress vertexAddr = 0;
            if (hasSkin && skinnedSimpleVertexBuffer_)
            {
                vertexAddr = skinnedSimpleVertexBuffer_->GetDeviceAddress();
            }

            geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true, vertexAddr);
            bottomAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, geometries);

            vertexOffset += vertexCount * sizeof(short) * 4;
            indexOffset += indexCount * sizeof(uint32_t);
            aabbOffset += sizeof(VkAabbPositionsKHR);
        }

        // Allocate the structures memory.
        const auto total = GetTotalRequirements(bottomAs_);

        bottomBuffer_.reset(new Buffer(Device(), total.accelerationStructureSize,
                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        bottomBufferMemory_.reset(new DeviceMemory(
            bottomBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        bottomScratchBuffer_.reset(new Buffer(Device(), total.buildScratchSize,
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        bottomScratchBufferMemory_.reset(new DeviceMemory(
            bottomScratchBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

        debugUtils.SetObjectName(bottomBuffer_->Handle(), "BLAS Buffer");
        debugUtils.SetObjectName(bottomBufferMemory_->Handle(), "BLAS Memory");
        debugUtils.SetObjectName(bottomScratchBuffer_->Handle(), "BLAS Scratch Buffer");
        debugUtils.SetObjectName(bottomScratchBufferMemory_->Handle(), "BLAS Scratch Memory");

        // Generate the structures.
        VkDeviceSize resultOffset = 0;
        VkDeviceSize scratchOffset = 0;

        for (size_t i = 0; i != bottomAs_.size(); ++i)
        {
            bottomAs_[i].Generate(commandBuffer, *bottomScratchBuffer_, scratchOffset, *bottomBuffer_, resultOffset);

            resultOffset += bottomAs_[i].BuildSizes().accelerationStructureSize;
            scratchOffset += bottomAs_[i].BuildSizes().buildScratchSize;

            debugUtils.SetObjectName(bottomAs_[i].Handle(), ("BLAS #" + std::to_string(i)).c_str());
        }
    }

    void RayTraceBaseRenderer::CreateTopLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();
        const uint32_t kMaxInstanceCount = 65535;
        
        instancesBuffer_.reset(new Buffer(Device(), kMaxInstanceCount * sizeof(VkAccelerationStructureInstanceKHR),
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ));
        instancesBufferMemory_.reset(new DeviceMemory(
            instancesBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        
        // Memory barrier for the bottom level acceleration structure builds.
        AccelerationStructure::InsertMemoryBarrier(commandBuffer);

        topAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, instancesBuffer_->GetDeviceAddress(),
                           kMaxInstanceCount);

        // Allocate the structure memory.
        const auto total = GetTotalRequirements(topAs_);

        topBuffer_.reset(new Buffer(Device(), total.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        topBufferMemory_.reset(new DeviceMemory(topBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

        topScratchBuffer_.reset(new Buffer(Device(), total.buildScratchSize,
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        topScratchBufferMemory_.reset(new DeviceMemory(
            topScratchBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

        debugUtils.SetObjectName(topBuffer_->Handle(), "TLAS Buffer");
        debugUtils.SetObjectName(topBufferMemory_->Handle(), "TLAS Memory");
        debugUtils.SetObjectName(topScratchBuffer_->Handle(), "TLAS Scratch Buffer");
        debugUtils.SetObjectName(topScratchBufferMemory_->Handle(), "TLAS Scratch Memory");
        debugUtils.SetObjectName(instancesBuffer_->Handle(), "TLAS Instances Buffer");
        debugUtils.SetObjectName(instancesBufferMemory_->Handle(), "TLAS Instances Memory");

        // Generate the structures.
        topAs_[0].Generate(commandBuffer, *topScratchBuffer_, 0, *topBuffer_, 0);

        debugUtils.SetObjectName(topAs_[0].Handle(), "TLAS");
    }
}
