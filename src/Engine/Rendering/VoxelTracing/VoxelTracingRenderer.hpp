#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <memory>

namespace Vulkan
{
    namespace PipelineCommon
    {
        class ZeroBindPipeline;
    }
}

namespace Vulkan::VoxelTracing
{
    class VoxelTracingRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(VoxelTracingRenderer)

        explicit VoxelTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
        ~VoxelTracingRenderer();

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        void ReloadShaders(const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles) override;
        FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_VoxelTracing); }
        bool RequiresObjectIdHistory() const override { return false; }

    private:
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
    };
}
