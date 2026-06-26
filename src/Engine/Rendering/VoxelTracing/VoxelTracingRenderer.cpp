#include "Engine/Rendering/VoxelTracing/VoxelTracingRenderer.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

namespace Vulkan::VoxelTracing
{
    VoxelTracingRenderer::VoxelTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender) :
        LogicRendererBase(baseRender)
    {
    }

    VoxelTracingRenderer::~VoxelTracingRenderer()
    {
        DeleteSwapChain();
    }

    void VoxelTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.VoxelTracing.comp.slang.spv", GetScene()));
    }

    void VoxelTracingRenderer::DeleteSwapChain()
    {
        deferredShadingPipeline_.reset();
    }

    void VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.InitializeBarriers(commandBuffer);

        {
            SCOPED_GPU_TIMER("shadingpass");
            deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(
                commandBuffer,
                Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(
                commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
    }
}
