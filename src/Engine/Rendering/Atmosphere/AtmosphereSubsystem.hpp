#pragma once

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/ResourceStateTracker.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>

namespace Vulkan
{
    class VulkanBaseRenderer;
    class RenderImage;
    class Buffer;
    class DeviceMemory;
    struct FRendererContract;

    namespace PipelineCommon
    {
        class ZeroBindPipeline;
    }
}

namespace Rendering::Atmosphere
{
    class AtmosphereSubsystem final
    {
    public:
        explicit AtmosphereSubsystem(Vulkan::VulkanBaseRenderer& renderer);
        ~AtmosphereSubsystem();

        void CreateDeviceResources();
        void DestroyDeviceResources();
        void CreateSwapChainPipelines();
        void DestroySwapChainPipelines();

        void BeginSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void PrepareView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
                         const Vulkan::FRendererContract& contract);
        void ApplyToView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
                         const Vulkan::FRendererContract& contract);

        bool Enabled() const;
        VkDeviceAddress ParamsAddress() const;
        glm::vec3 TransmittanceToSun(float cameraAltitudeKm, float sunZenithCosine) const;

    private:
        Assets::FAtmosphereParams BuildParams() const;
        void WriteParams(const Assets::FAtmosphereParams& params);
        void Dispatch2D(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                        Vulkan::PipelineCommon::ZeroBindPipeline& pipeline,
                        Vulkan::RenderImage& target, uint32_t slot, VkExtent2D extent,
                        std::string_view passName);
        void TransitionImage(VkCommandBuffer commandBuffer, Vulkan::RenderImage& image, uint32_t slot,
                             Vulkan::PipelineCommon::ERenderStage stages,
                             Vulkan::PipelineCommon::EResourceAccess access,
                             VkImageLayout layout, bool discard, std::string_view passName);

        Vulkan::VulkanBaseRenderer& renderer_;
        std::unique_ptr<Vulkan::Buffer> paramsBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> paramsMemory_;
        std::unique_ptr<Vulkan::RenderImage> transmittanceLut_;
        std::unique_ptr<Vulkan::RenderImage> multiScatterLut_;
        std::unique_ptr<Vulkan::RenderImage> skyViewLut_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> transmittancePipeline_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> multiScatterPipeline_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> skyViewPipeline_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> aerialPerspectivePipeline_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> compositePipeline_;
        Vulkan::PipelineCommon::FResourceStateTracker lutStates_;
        Assets::FAtmosphereParams lastParams_{};
        bool haveLastParams_ = false;
        bool staticLutsDirty_ = true;
        bool aerialPerspectiveReady_ = false;
        glm::vec3 lastProjectedSunDirection_{};
        bool haveProjectedSky_ = false;
    };
}
