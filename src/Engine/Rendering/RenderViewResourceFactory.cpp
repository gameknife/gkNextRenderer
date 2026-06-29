#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/RenderViewResourceFactory.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

namespace Vulkan
{
    RenderViewResourceFactory::RenderViewResourceFactory(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
    }

    std::unique_ptr<FrameBuffer> RenderViewResourceFactory::RebuildVisibilityFramebuffer(
        RenderView& view,
        const VkExtent2D extent)
    {
        renderer_.CreateRenderTargetBank(view.RtBankBase(), extent);
        auto frameBuffer = std::make_unique<FrameBuffer>(
            extent,
            renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(),
            renderer_.overlay_.visibilityPipeline->RenderPass());
        view.SetAllocatedVisibilityFramebuffer(frameBuffer.get(), extent);
        return frameBuffer;
    }

    std::unique_ptr<RenderImage> RenderViewResourceFactory::CreateSampledColorImage(
        const VkExtent2D extent,
        const char* debugName)
    {
        return std::make_unique<RenderImage>(
            renderer_.Device(),
            extent,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            false,
            debugName);
    }

    std::unique_ptr<Sampler> RenderViewResourceFactory::CreateClampSampler()
    {
        Vulkan::SamplerConfig samplerConfig{};
        samplerConfig.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerConfig.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerConfig.AnisotropyEnable = false;
        return std::make_unique<Vulkan::Sampler>(renderer_.Device(), samplerConfig);
    }

    void RenderViewResourceFactory::BindSampledColorImage(
        const uint32_t sampleSlot,
        RenderImage& image,
        Sampler& sampler)
    {
        renderer_.ctx_.globalTexturePool->BindSampleTexture(sampleSlot, image.GetImageView(), sampler);
    }
}
