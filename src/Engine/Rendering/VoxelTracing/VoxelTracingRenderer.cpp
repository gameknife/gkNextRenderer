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

    void VoxelTracingRenderer::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (deferredShadingPipeline_)
        {
            deferredShadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
    }

    void VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.ImportActiveViewImagesGeneral({Assets::Bindless::RT_DENOISED}, "scene image initialization");
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

        {
            SCOPED_GPU_TIMER("shadingpass");
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_DENOISED, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            }, "voxel tracing shading");
            deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(
                commandBuffer,
                Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
        }
    }
}
