#pragma once

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/TemporalPostChain.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan::PathTracing
{

    class PathTracingRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(PathTracingRenderer);

        PathTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender): LogicRendererBase(baseRender) {}
        virtual ~PathTracingRenderer();

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

        struct FSharcBuffer
        {
            std::unique_ptr<Vulkan::Buffer> buffer;
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            VkDeviceSize size = 0;
        };

        struct FSharcState
        {
            const Assets::Scene* ownerScene = nullptr;
            uint64_t ownerSceneGeneration = 0;
            FSharcBuffer hashEntries;
            FSharcBuffer lockBuffer;
            FSharcBuffer accumulation;
            FSharcBuffer resolved;
            FSharcBuffer parameters;
            uint32_t entriesPow2 = 0;
            uint32_t entryCount = 0;
            uint32_t lastFrameIndex = ~0u;
            glm::vec4 lastCameraPosition = glm::vec4(0.0f);
            bool hasLastCameraPosition = false;
            glm::vec4 lastSunDirection = glm::vec4(0.0f);
            glm::vec4 lastSunColor = glm::vec4(0.0f);
            uint32_t lastSkyIdx = 0;
            float lastSkyIntensity = 0.0f;
            float lastSkyRotation = 0.0f;
            uint32_t lastHasSun = 0;
            uint32_t lastHasSky = 0;
            bool hasLastLightingState = false;
            bool pendingClear = false;
        };

        // Screen-space ReSTIR DI reservoirs (docs/designs/pathtracing-restir-design.md).
        // Extent-sized double buffer, primary view only; recreated when the render extent changes.
        struct FRestirState
        {
            FSharcBuffer reservoirPing;
            FSharcBuffer reservoirPong;
            FSharcBuffer parameters;
            VkExtent2D extent{};
            bool pendingClear = false;
            uint32_t lastFrameIndex = ~0u;
            uint64_t lastLightsGeneration = 0;
            // Stamped into gpuScene.CustomData1 per dispatch (bit0 parity, bit1 temporal
            // valid): these are race-critical and must ride the recorded push constant, not
            // the host-visible parameter buffer.
            uint32_t frameStamp = 0;
        };

    private:

        // individual textures
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> rayTracingPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> sharcUpdatePipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> sharcQueryPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> restirSpatialPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> sharcResolvePipeline_;
        PipelineCommon::TemporalPostChain temporalPostChain_;

        FSharcState sharc_;
        FRestirState restir_;
        // FPathTracingExtras table shared by SHARC and ReSTIR, reached via GPUScene.ReservedAddress0.
        // Content is view-invariant (fixed device addresses) so per-Render() rewrites cannot race
        // across views; per-view gating happens shader-side via the recorded push constant.
        FSharcBuffer extras_;
        Assets::FPathTracingExtras lastExtrasContent_{};

        void EnsureSharcPipelines();
        void EnsureSharcResources();
        void UpdateSharcParameters();
        void ClearSharcResources(VkCommandBuffer commandBuffer);
        void InsertSharcBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) const;
        void EnsureRestirResources(const VkExtent2D& extent);
        void UpdateRestirParameters();
        void ClearRestirResources(VkCommandBuffer commandBuffer);
        void InsertRestirBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) const;
        void UpdateExtrasTable(bool sharcActive);
        bool IsOfflineProgressiveRenderActive() const;
        bool IsEffectiveSharcEnabled() const;
        bool IsRestirEnabled() const;
    };

}
