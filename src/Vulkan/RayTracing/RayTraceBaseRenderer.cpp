#include "RayTraceBaseRenderer.hpp"
#include "BottomLevelAccelerationStructure.hpp"
#include "DeviceProcedures.hpp"
#include "RayTraceBaseRenderer.hpp"
#include "ShaderBindingTable.hpp"
#include "TopLevelAccelerationStructure.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include <chrono>
#include <iomanip>
#include <fmt/format.h>
#include <numeric>

#include "Runtime/Engine.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredPipeline.hpp"
#include "Vulkan/HybridDeferred/HybridDeferredRenderer.hpp"
#include "Vulkan/LegacyDeferred/LegacyDeferredRenderer.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredRenderer.hpp"
#include "Vulkan/RayQuery/RayQueryRenderer.hpp"

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
                                               const bool enableValidationLayers) :
        Vulkan::VulkanBaseRenderer(window, presentMode, enableValidationLayers)
    {
        supportRayCast_ = true;
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
                                      VK_KHR_RAY_QUERY_EXTENSION_NAME
                                  });

#if WITH_OIDN
        // Required extensions.
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
#if WIN32 && !defined(__MINGW32__)
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#endif
#endif

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

        //topScratchBuffer_.reset();
        //topScratchBufferMemory_.reset();
        bottomScratchBuffer_.reset();
        bottomScratchBufferMemory_.reset();

        const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();
        fmt::print("- built acceleration structures in {:.2f}ms\n", elapsed * 1000.f);
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
        
        rayCastBuffer_.reset(new Assets::RayCastBuffer(CommandPool()));
#if !ANDROID
        raycastPipeline_.reset(new PipelineCommon::RayCastPipeline(Device().GetDeviceProcedures(), rayCastBuffer_->Buffer(), topAs_[0], GetScene()));
#endif
        ambientGenPipeline_.reset(new PipelineCommon::AmbientGenPipeline(SwapChain(), Device().GetDeviceProcedures(), GetScene().AmbientCubeBuffer(), topAs_[0], UniformBuffers(), GetScene()));
        directLightGenPipeline_.reset(new PipelineCommon::DirectLightGenPipeline(SwapChain(), Device().GetDeviceProcedures(), GetScene().AmbientCubeBuffer(), topAs_[0], UniformBuffers(), GetScene()));
    }

    void RayTraceBaseRenderer::DeleteSwapChain()
    {
        Vulkan::VulkanBaseRenderer::DeleteSwapChain();

        rayCastBuffer_.reset();
        raycastPipeline_.reset();
        ambientGenPipeline_.reset();
        directLightGenPipeline_.reset();
    }

    void RayTraceBaseRenderer::AfterRenderCmd()
    {
        VulkanBaseRenderer::AfterRenderCmd();

        if(supportRayCast_)
        {
            for ( int i = 0; i < rayRequested_.size(); i++)
            {
                rayCastBuffer_->rayCastIO[i].Context = rayRequested_[i].context;
            }
            
            rayCastBuffer_->SyncWithGPU(); // readback last frame, request this frame

            for ( int i = 0; i < rayFetched_.size(); i++)
            {
                if (rayFetched_[i].callback)
                {
                    rayFetched_[i].callback(rayCastBuffer_->rayCastIO[i].Result);
                }
            }
            rayFetched_ = rayRequested_;
            rayRequested_.clear();
        }
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
            auto& Node = nodeTrans[i];
            instances.push_back(TopLevelAccelerationStructure::CreateInstance(
                bottomAs_[Node.modelId], glm::transpose(Node.worldTS), Node.instanceId, true));
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

    void RayTraceBaseRenderer::SetRaycastRay(glm::vec3 org, glm::vec3 dir, std::function<bool(Assets::RayCastResult)> callback)
    {
        Assets::RayCastContext context;
        context.Origin = glm::vec4(org, 1);
        context.Direction = glm::vec4(dir, 0);
        rayRequested_.push_back({context, callback});
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

    void RayTraceBaseRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (tlasUpdateRequest_ > 0)
        {
            topAs_[0].Update(commandBuffer, tlasUpdateRequest_);
            tlasUpdateRequest_ = 0;
        }
        VulkanBaseRenderer::Render(commandBuffer, imageIndex);

#if !ANDROID
        if(supportRayCast_)
        {
            SCOPED_GPU_TIMER("raycast");

            VkMemoryBarrier memoryBarrier = {};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.pNext = nullptr;
            memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
            
            VkDescriptorSet DescriptorSets[] = {raycastPipeline_->DescriptorSet(0)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, raycastPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    raycastPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, 1, 1, 1);
        }
#endif
        
        if(supportRayCast_ && lastUBO.BakeWithGPU && CurrentLogicRendererType() != ERT_PathTracing && CurrentLogicRendererType() != ERT_LegacyDeferred)
        {
#if !ANDROID
            const int cubesPerGroup = 32;
#else
            const int cubesPerGroup = 1024;
#endif      
            
            const int count = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
            const int group = count / cubesPerGroup;

            // 每32个cube一个group

            
#if !ANDROID
            int temporalFrames = 60;
            switch (NextEngine::GetInstance()->GetUserSettings().BakeSpeedLevel)
            {
            case 0:
                temporalFrames = 1;
                break;
            case 1:
                temporalFrames = 60;
                break;
            case 2:
                temporalFrames = 300;
                break;
            default:
                temporalFrames = 60;
                break;
            }
#else
            int temporalFrames = 625;
#endif
            // 我们计划在temporalFrames帧内完成, 所以每帧处理1/60个group，并设置offset
            int frame = (int)(frameCount_ % temporalFrames);
            int groupPerFrame = group / temporalFrames;
            int offset = frame * groupPerFrame;
            int offsetInCubes = offset * cubesPerGroup;

            {
                SCOPED_GPU_TIMER("ambient di");
                VkDescriptorSet DescriptorSets[] = {directLightGenPipeline_->DescriptorSet(0)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, directLightGenPipeline_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        directLightGenPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);

                // bind the global bindless set
                static const uint32_t k_bindless_set = 1;
                VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
                vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, directLightGenPipeline_->PipelineLayout().Handle(), k_bindless_set,
                                         1, GlobalDescriptorSets, 0, nullptr );
                
                glm::uvec2 pushConst = { offsetInCubes, 1 };

                vkCmdPushConstants(commandBuffer, directLightGenPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(glm::uvec2), &pushConst);
            
                vkCmdDispatch(commandBuffer, groupPerFrame, 1, 1);    
            }
            
            // {
            //     SCOPED_GPU_TIMER("ambient ii");
            //     VkDescriptorSet DescriptorSets[] = {ambientGenPipeline_->DescriptorSet(0)};
            //     vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ambientGenPipeline_->Handle());
            //     vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            //                             ambientGenPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            //
            //     // bind the global bindless set
            //     static const uint32_t k_bindless_set = 1;
            //     VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
            //     vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ambientGenPipeline_->PipelineLayout().Handle(), k_bindless_set,
            //                              1, GlobalDescriptorSets, 0, nullptr );
            //     
            //     glm::uvec2 pushConst = { offsetInCubes, 1 };
            //
            //     vkCmdPushConstants(commandBuffer, ambientGenPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
            //                        0, sizeof(glm::uvec2), &pushConst);
            //
            //     vkCmdDispatch(commandBuffer, groupPerFrame, 1, 1);    
            // }
            // 这里可以做一个模糊，或者说降噪，这个对于CPU GEN也可以
        }
    }

    void RayTraceBaseRenderer::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();

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
        
        for (auto& model : scene.Models())
        {
            const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
            const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
            BottomLevelGeometry geometries;
            geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true);
            bottomAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, geometries);

            vertexOffset += vertexCount * sizeof(Assets::GPUVertex);
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
        
        // Create and copy instances buffer (do it in a separate one-time synchronous command buffer).
        // BufferUtil::CreateDeviceBuffer(CommandPool(), "TLAS Instances",
        //                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        //                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, instances, instancesBuffer_,
        //                                instancesBufferMemory_);

        // buffer_.reset(new Vulkan::Buffer(device, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        // memory_.reset(new Vulkan::DeviceMemory(buffer_->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        
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
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR));
        topBufferMemory_.reset(new DeviceMemory(topBuffer_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

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
