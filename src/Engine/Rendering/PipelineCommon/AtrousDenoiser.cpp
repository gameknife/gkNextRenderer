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
        struct FFusedAtrousPushConstants
        {
            uint32_t diffuseInSlot;
            uint32_t diffuseOutSlot;
            uint32_t specularInSlot;
            uint32_t specularOutSlot;
            uint32_t stepSize;
            uint32_t firstIteration;
            uint32_t filterDiffuse;
            uint32_t filterSpecular;
            float sigmaDepth;
            float sigmaLuma;
            float sigmaNormalPower;
            float specFootprintScale;
        };
    }

    AtrousDenoiser::AtrousDenoiser() = default;
    AtrousDenoiser::~AtrousDenoiser() = default;

    void AtrousDenoiser::CreateSwapChain(const SwapChain& swapChain)
    {
        pipeline_.reset(new ZeroBindCustomPushConstantPipeline(
            swapChain, "assets/shaders/Process.AtrousWavelet.comp.slang.spv", sizeof(FFusedAtrousPushConstants)));
    }

    void AtrousDenoiser::DeleteSwapChain()
    {
        pipeline_.reset();
    }

    void AtrousDenoiser::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (pipeline_)
        {
            pipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
    }

    void AtrousDenoiser::Run(
        VulkanBaseRenderer& baseRenderer,
        const SwapChain& swapChain,
        VkCommandBuffer commandBuffer,
        const Runtime::Config::UserSettings& settings) const
    {
        (void)swapChain;
        const int diffuseIterations = settings.Denoiser ? std::clamp(settings.DenoiseAtrousIterations, 0, 6) : 0;
        const int specularIterations = settings.Denoiser ? std::clamp(settings.DenoiseAtrousSpecularIterations, 0, 6) : 0;
        const int maxIterations = std::max(diffuseIterations, specularIterations);
        if (maxIterations <= 0 || !pipeline_)
        {
            return;
        }

        const std::array<uint32_t, 2> diffusePingPong{Assets::Bindless::RT_ATROUS_PING, Assets::Bindless::RT_ATROUS_PONG};
        const std::array<uint32_t, 2> specularPingPong{Assets::Bindless::RT_ATROUS_SPEC_PING, Assets::Bindless::RT_ATROUS_SPEC_PONG};
        const VkExtent2D activeExtent = baseRenderer.ActiveViewRenderExtent();
        const uint32_t dispatchX = Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8);
        const uint32_t dispatchY = Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8);

        auto getSourceSlot = [](int iteration, uint32_t accumSlot, const std::array<uint32_t, 2>& pingPong)
        {
            return (iteration == 0) ? accumSlot : pingPong[(iteration - 1) & 1];
        };

        auto getOutputSlot = [](int iteration, int iterations, uint32_t finalSlot, const std::array<uint32_t, 2>& pingPong)
        {
            return (iteration == iterations - 1) ? finalSlot : pingPong[iteration & 1];
        };

        for (int i = 0; i < maxIterations; ++i)
        {
            const bool filterDiffuse = i < diffuseIterations;
            const bool filterSpecular = i < specularIterations;

            FFusedAtrousPushConstants push{};
            push.diffuseInSlot = getSourceSlot(i, Assets::Bindless::RT_ACCUMULATE_DIFFUSE, diffusePingPong);
            push.diffuseOutSlot = getOutputSlot(i, diffuseIterations, Assets::Bindless::RT_ATROUS_OUT, diffusePingPong);
            push.specularInSlot = getSourceSlot(i, Assets::Bindless::RT_ACCUMULATE_SPECULAR, specularPingPong);
            push.specularOutSlot = getOutputSlot(i, specularIterations, Assets::Bindless::RT_ATROUS_SPEC_OUT, specularPingPong);
            push.stepSize = 1u << i;
            push.firstIteration = (i == 0) ? 1u : 0u;
            push.filterDiffuse = filterDiffuse ? 1u : 0u;
            push.filterSpecular = filterSpecular ? 1u : 0u;
            push.sigmaDepth = settings.DenoiseSigmaDepth;
            push.sigmaLuma = settings.DenoiseAtrousSigmaLuma;
            push.sigmaNormalPower = settings.DenoiseAtrousNormalPower;
            push.specFootprintScale = settings.DenoiseSpecFootprint;

            baseRenderer.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_NORMAL, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
            }, "atrous guides");
            if (filterDiffuse)
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {push.diffuseInSlot, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {push.diffuseOutSlot, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "atrous diffuse iteration");
            }
            if (filterSpecular)
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {push.specularInSlot, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {push.specularOutSlot, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "atrous specular iteration");
            }

            pipeline_->BindPipeline(commandBuffer, &push, baseRenderer.ActiveViewBankBase());
            vkCmdDispatch(commandBuffer, dispatchX, dispatchY, 1);
        }
    }
}
