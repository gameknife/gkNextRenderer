#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/RestirDI.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan::PipelineCommon
{
    namespace
    {
        static_assert(sizeof(Assets::FTracingExtras) == 64);
        static_assert(sizeof(Assets::FRestirReservoir) == 16);
        static_assert(sizeof(Assets::FRestirRuntimeParameters) == 48);

        template <typename TBufferResource>
        void CreateBuffer(CommandPool& commandPool, const char* name, VkDeviceSize size,
                          VkMemoryPropertyFlags memoryFlags, TBufferResource& outBuffer)
        {
            BufferUtil::CreateDeviceBufferLocal(
                commandPool, name,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                memoryFlags, static_cast<size_t>(size), outBuffer.buffer, outBuffer.memory);
            outBuffer.size = size;
        }

        template <typename TBufferResource>
        void WriteHostVisibleBuffer(TBufferResource& dst, const void* data, size_t size)
        {
            void* mapped = dst.memory->Map(0, size);
            std::memcpy(mapped, data, size);
            dst.memory->Unmap();
        }
    }

    RestirDI::RestirDI(VulkanBaseRenderer& baseRenderer): baseRenderer_(baseRenderer)
    {
    }

    RestirDI::~RestirDI() = default;

    bool RestirDI::HasResources() const
    {
        return reservoirPing_.buffer != nullptr;
    }

    VkDeviceAddress RestirDI::ReservoirPingAddress() const
    {
        return reservoirPing_.buffer ? reservoirPing_.buffer->GetDeviceAddress() : 0;
    }

    VkDeviceAddress RestirDI::ReservoirPongAddress() const
    {
        return reservoirPong_.buffer ? reservoirPong_.buffer->GetDeviceAddress() : 0;
    }

    VkDeviceAddress RestirDI::ParametersAddress() const
    {
        return parameters_.buffer ? parameters_.buffer->GetDeviceAddress() : 0;
    }

    VkDeviceAddress RestirDI::ResourceTableAddress() const
    {
        return resourceTable_.buffer ? resourceTable_.buffer->GetDeviceAddress() : 0;
    }

    void RestirDI::EnsureResources(const VkExtent2D& extent)
    {
        if (HasResources() && extent_.width == extent.width && extent_.height == extent.height)
        {
            return;
        }

        reservoirPing_.Reset();
        reservoirPong_.Reset();
        parameters_.Reset();
        resourceTable_.Reset();
        extent_ = extent;
        lastFrameIndex_ = ~0u;
        lastLightsGeneration_ = 0;
        lastHistoryGeneration_ = 0;
        frameStamp_ = 0;

        const VkDeviceSize reservoirBytes =
            sizeof(Assets::FRestirReservoir) * VkDeviceSize(extent.width) * VkDeviceSize(extent.height);
        CreateBuffer(baseRenderer_.CommandPool(), "RestirReservoirPing", reservoirBytes,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reservoirPing_);
        CreateBuffer(baseRenderer_.CommandPool(), "RestirReservoirPong", reservoirBytes,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reservoirPong_);
        CreateBuffer(baseRenderer_.CommandPool(), "RestirParameters",
                     sizeof(Assets::FRestirRuntimeParameters),
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     parameters_);
        CreateBuffer(baseRenderer_.CommandPool(), "RestirTracingExtras", sizeof(Assets::FTracingExtras),
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     resourceTable_);
        UpdateResourceTable();
        pendingClear_ = true;

        SPDLOG_INFO("ReSTIR reservoirs allocated: {}x{}, memory={:.2f} MiB", extent.width, extent.height,
                    static_cast<double>(reservoirBytes * 2) / (1024.0 * 1024.0));
    }

    void RestirDI::UpdateResourceTable()
    {
        Assets::FTracingExtras extras{};
        extras.RestirReservoirPing = ReservoirPingAddress();
        extras.RestirReservoirPong = ReservoirPongAddress();
        extras.RestirParameters = ParametersAddress();
        WriteHostVisibleBuffer(resourceTable_, &extras, sizeof(extras));
    }

    void RestirDI::UpdateParameters(const bool reuseAllowed)
    {
        const auto& settings = baseRenderer_.FrameSettings().userSettings;
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(baseRenderer_.FrameCount(), 0));
        const uint64_t lightsGeneration = baseRenderer_.GetScene().LightsGeneration();
        const uint64_t historyGeneration = baseRenderer_.ActiveRenderView().State().historyGeneration;
        const bool temporalValid =
            lastFrameIndex_ != ~0u && frameIndex == lastFrameIndex_ + 1 &&
            historyGeneration == lastHistoryGeneration_ &&
            lightsGeneration == lastLightsGeneration_ && !pendingClear_;

        lastFrameIndex_ = frameIndex;
        lastHistoryGeneration_ = historyGeneration;
        lastLightsGeneration_ = lightsGeneration;
        frameStamp_ = temporalValid ? 0x2u : 0u;

        Assets::FRestirRuntimeParameters params{};
        params.FrameIndex = frameIndex;
        params.DebugMode = static_cast<uint32_t>(std::max(settings.RestirDebugMode, 0));
        params.Flags = (settings.RestirTemporal && reuseAllowed ? 0x1u : 0u) |
                       (settings.RestirSpatial && reuseAllowed ? 0x2u : 0u) |
                       (temporalValid ? 0x4u : 0u);
        params.LightsGeneration = static_cast<uint32_t>(lightsGeneration);
        params.ReservoirWidth = extent_.width;
        params.ReservoirHeight = extent_.height;
        params.InitialCandidates = std::clamp(settings.RestirCandidates, 1u, 64u);
        params.TemporalMClamp = std::max(settings.RestirMClamp, 1u);
        params.SpatialRadius = std::max(settings.RestirSpatialRadius, 1.0f);
        params.SpatialSamples = std::clamp(settings.RestirSpatialSamples, 1u, 16u);
        WriteHostVisibleBuffer(parameters_, &params, sizeof(params));
    }

    void RestirDI::ClearResources(VkCommandBuffer commandBuffer)
    {
        if (!pendingClear_)
        {
            return;
        }

        vkCmdFillBuffer(commandBuffer, reservoirPing_.buffer->Handle(), 0, reservoirPing_.size, 0);
        vkCmdFillBuffer(commandBuffer, reservoirPong_.buffer->Handle(), 0, reservoirPong_.size, 0);
        InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        pendingClear_ = false;
    }

    void RestirDI::Prepare(VkCommandBuffer commandBuffer, const VkExtent2D& extent,
                           const bool reuseAllowed)
    {
        EnsureResources(extent);
        UpdateParameters(reuseAllowed);
        ClearResources(commandBuffer);
    }

    void RestirDI::InsertBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask,
                                 VkAccessFlags dstAccessMask) const
    {
        if (!HasResources())
        {
            return;
        }

        const VkPipelineStageFlags srcStage =
            (srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0 ? VK_PIPELINE_STAGE_TRANSFER_BIT :
                                                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        BufferMemoryBarrier::Insert(commandBuffer, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(reservoirPing_.buffer->Handle(), srcAccessMask, dstAccessMask),
            BufferMemoryBarrier::Make(reservoirPong_.buffer->Handle(), srcAccessMask, dstAccessMask),
        });
    }
}
