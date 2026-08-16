#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBuildPass.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBufferLayout.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::PipelineCommon
{
    void SurfaceBuildPass::CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene)
    {
        pipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Core.SurfaceBuild.comp.slang.spv", scene);
    }

    void SurfaceBuildPass::DeleteSwapChain()
    {
        pipeline_.reset();
    }

    void SurfaceBuildPass::Run(
        VulkanBaseRenderer& baseRender,
        VkCommandBuffer commandBuffer,
        const Assets::GPUScene& gpuScene,
        const bool writeSpecularAlbedo)
    {
        if (!pipeline_)
        {
            return;
        }

        constexpr auto compute = ERenderStage::Compute;
        constexpr auto shaderWrite = EResourceAccess::ShaderWrite;
        baseRender.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, compute, shaderWrite},
            {Assets::Bindless::RT_NORMAL, compute, shaderWrite},
            {Assets::Bindless::RT_ALBEDO, compute, shaderWrite},
            {Assets::Bindless::RT_OBJECTID_0, compute, shaderWrite},
            {Assets::Bindless::RT_MOTIONVECTOR, compute, shaderWrite},
            {Assets::Bindless::RT_BSDF_DATA, compute, shaderWrite},
            // Common.CalculateMotionVector read-modify-writes the moment counter.
            {Assets::Bindless::RT_MOTIONMOMENT, compute,
             EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite},
        }, "primary surface build");
        if (writeSpecularAlbedo)
        {
            baseRender.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SPECULAR_ALBEDO, compute, shaderWrite},
            }, "primary surface build");
        }

        SCOPED_GPU_TIMER("surface build");
        const uint32_t buildFlags = writeSpecularAlbedo ? kSurfaceBuildWriteSpecularAlbedo : 0u;
        pipeline_->BindPipeline(commandBuffer, gpuScene, gpuScene.CustomData0, buildFlags,
                                gpuScene.CustomData2);
        const VkExtent2D extent = baseRender.ActiveViewRenderExtent();
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                      Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
    }
}
