#include "Engine/Rendering/PipelineCommon/AtrousDenoiser.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <algorithm>
#include <array>

namespace Vulkan::PipelineCommon
{
    namespace
    {
        // Mirrors PushConsts in Process.AtrousWavelet.comp.slang.
        struct FAtrousPushConstants
        {
            uint32_t inColorSlot;
            uint32_t outSlot;
            uint32_t stepSize;
            uint32_t firstIteration;
            float sigmaDepth;
            float sigmaLuma;
            float sigmaNormalPower;
            uint32_t isSpecular;
            float specFootprintScale;
            float pad0;
            float pad1;
        };
    }

    AtrousDenoiser::AtrousDenoiser() = default;
    AtrousDenoiser::~AtrousDenoiser() = default;

    void AtrousDenoiser::CreateSwapChain(const SwapChain& swapChain)
    {
        pipeline_.reset(new ZeroBindCustomPushConstantPipeline(
            swapChain, "assets/shaders/Process.AtrousWavelet.comp.slang.spv", sizeof(FAtrousPushConstants)));
    }

    void AtrousDenoiser::DeleteSwapChain()
    {
        pipeline_.reset();
    }

    void AtrousDenoiser::Run(
        VulkanBaseRenderer& baseRenderer,
        const SwapChain& swapChain,
        VkCommandBuffer commandBuffer,
        const Runtime::Config::UserSettings& settings) const
    {
        const int atrousIterations = settings.Denoiser ? std::clamp(settings.DenoiseAtrousIterations, 0, 6) : 0;
        if (atrousIterations <= 0 || !pipeline_)
        {
            return;
        }

        const std::array<uint32_t, 2> pingPong{Assets::Bindless::RT_ATROUS_PING, Assets::Bindless::RT_ATROUS_PONG};
        const uint32_t dispatchX = Utilities::Math::GetSafeDispatchCount(swapChain.RenderExtent().width, 8);
        const uint32_t dispatchY = Utilities::Math::GetSafeDispatchCount(swapChain.RenderExtent().height, 8);

        auto runAtrous = [&](uint32_t accumSlot, uint32_t finalSlot, bool isSpecular)
        {
            for (int i = 0; i < atrousIterations; ++i)
            {
                FAtrousPushConstants push{};
                push.inColorSlot = (i == 0) ? accumSlot : pingPong[(i - 1) & 1];
                push.outSlot = (i == atrousIterations - 1) ? finalSlot : pingPong[i & 1];
                push.stepSize = 1u << i;
                push.firstIteration = (i == 0) ? 1u : 0u;
                push.sigmaDepth = settings.DenoiseSigmaDepth;
                push.sigmaLuma = settings.DenoiseAtrousSigmaLuma;
                push.sigmaNormalPower = settings.DenoiseAtrousNormalPower;
                push.isSpecular = isSpecular ? 1u : 0u;
                push.specFootprintScale = settings.DenoiseSpecFootprint;
                push.pad0 = 0.0f;
                push.pad1 = 0.0f;

                pipeline_->BindPipeline(commandBuffer, &push);
                vkCmdDispatch(commandBuffer, dispatchX, dispatchY, 1);

                baseRenderer.GetStorageImage(push.outSlot)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            }
        };

        runAtrous(Assets::Bindless::RT_ACCUMLATE_DIFFUSE, Assets::Bindless::RT_ATROUS_OUT, false);

        // Serialize the shared ping/pong scratch before the specular pass reuses it (WAR).
        baseRenderer.GetStorageImage(Assets::Bindless::RT_ATROUS_PING)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        baseRenderer.GetStorageImage(Assets::Bindless::RT_ATROUS_PONG)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

        runAtrous(Assets::Bindless::RT_ACCUMLATE_SPECULAR, Assets::Bindless::RT_ATROUS_SPEC_OUT, true);
    }
}
