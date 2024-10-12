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

    LogicRendererBase::LogicRendererBase(RayTracing::RayTraceBaseRenderer& baseRender):baseRender_(baseRender)
    {
        
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

    void RayTraceBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        switch (type)
        {
            case ERendererType::ERT_PathTracing:
                logicRenderers_.push_back( std::make_unique<RayQueryRenderer>(*this) );
                break;
            case ERendererType::ERT_Hybrid:
                logicRenderers_.push_back( std::make_unique<HybridDeferred::HybridDeferredRenderer>(*this) );
                break;
        case ERendererType::ERT_ModernDeferred:
                logicRenderers_.push_back( std::make_unique<ModernDeferred::ModernDeferredRenderer>(*this) );
                break;
        case ERendererType::ERT_LegacyDeferred:
                logicRenderers_.push_back( std::make_unique<LegacyDeferred::LegacyDeferredRenderer>(*this) );
                break;
            default:
                assert(false);
        }
        currentLogicRenderer_ = type;
    }

    void RayTraceBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        currentLogicRenderer_ = type;
    }

    void RayTraceBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        supportRayTracing_ = true;
        
        // Required extensions.
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                                      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                      VK_KHR_RAY_QUERY_EXTENSION_NAME
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

        topScratchBuffer_.reset();
        topScratchBufferMemory_.reset();
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
        

        rayCastBuffer_.reset(new Assets::RayCastBuffer(Device()));
#if !ANDROID
        raycastPipeline_.reset(new PipelineCommon::RayCastPipeline(Device().GetDeviceProcedures(), rayCastBuffer_->Buffer(), topAs_[0], GetScene()));
#endif

        for( auto& logicRenderer : logicRenderers_ )
        {
            logicRenderer->CreateSwapChain();
        }
    }

    void RayTraceBaseRenderer::DeleteSwapChain()
    {
        for( auto& logicRenderer : logicRenderers_ )
        {
            logicRenderer->DeleteSwapChain();
        }
        
        Vulkan::VulkanBaseRenderer::DeleteSwapChain();

        rayCastBuffer_.reset();
        raycastPipeline_.reset();
    }

    void RayTraceBaseRenderer::AfterRenderCmd()
    {
        VulkanBaseRenderer::AfterRenderCmd();

        if(supportRayCast_)
        {
            rayCastBuffer_->SetContext(cameraCenterCastContext_);
            rayCastBuffer_->SyncWithGPU();
            cameraCenterCastResult_ = rayCastBuffer_->GetResult();
        }
        else
        {
            cameraCenterCastResult_ = Assets::RayCastResult();
        }
    }


    bool RayTraceBaseRenderer::GetFocusDistance(float& distance) const
    {
        if(cameraCenterCastResult_.Hitted)
        {
            distance = cameraCenterCastResult_.T;
        }

        return cameraCenterCastResult_.Hitted;
    }

    bool RayTraceBaseRenderer::GetLastRaycastResult(Assets::RayCastResult& result) const
    {
        result = cameraCenterCastResult_;
        return cameraCenterCastResult_.Hitted;
    }

    void RayTraceBaseRenderer::SetRaycastRay(glm::vec3 org, glm::vec3 dir) const
    {
        cameraCenterCastContext_.Origin = glm::vec4(org, 1);
        cameraCenterCastContext_.Direction = glm::vec4(dir, 0);
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
        if( currentLogicRenderer_ < logicRenderers_.size() )
        {
            logicRenderers_[currentLogicRenderer_]->Render(commandBuffer, imageIndex);
        }

        if(supportRayCast_)
        {
            SCOPED_GPU_TIMER("raycast");
            
            VkDescriptorSet DescriptorSets[] = {raycastPipeline_->DescriptorSet(0)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, raycastPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    raycastPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, 1, 1, 1);
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

            model.Procedural()
                ? geometries.AddGeometryAabb(scene, aabbOffset, 1, true)
                : geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true);

            bottomAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, geometries);

            vertexOffset += vertexCount * sizeof(Assets::Vertex);
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

        // Top level acceleration structure
        std::vector<VkAccelerationStructureInstanceKHR> instances;

        // Hit group 0: triangles
        // Hit group 1: procedurals
        for (const auto& node : scene.Nodes())
        {
            instances.push_back(TopLevelAccelerationStructure::CreateInstance(
                bottomAs_[node.GetModel()], glm::transpose(node.WorldTransform()), node.GetModel(),  node.IsProcedural() ? 1 : 0));
        }

        // Create and copy instances buffer (do it in a separate one-time synchronous command buffer).
        BufferUtil::CreateDeviceBuffer(CommandPool(), "TLAS Instances",
                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, instances, instancesBuffer_,
                                       instancesBufferMemory_);

        // Memory barrier for the bottom level acceleration structure builds.
        AccelerationStructure::InsertMemoryBarrier(commandBuffer);

        topAs_.emplace_back(Device().GetDeviceProcedures(), *rayTracingProperties_, instancesBuffer_->GetDeviceAddress(),
                            static_cast<uint32_t>(instances.size()));

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
