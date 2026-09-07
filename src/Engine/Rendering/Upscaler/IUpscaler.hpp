#pragma once

#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"

namespace Rendering::Upscaler
{
    class IUpscaler
    {
    public:
        virtual ~IUpscaler() = default;

        virtual void OnDeviceCreated(const FDeviceInfo& deviceInfo, FFeatureCaps& caps) = 0;
        // Compile provider-owned pipelines even when this provider is not the selected upscaler.
        // Startup warm-up deliberately leaves frame-sized history resources unallocated.
        virtual void WarmupPipelines() {}
        // Called after swapchain selection resolves. Providers must keep GPU
        // resources only while one of their types is active.
        virtual void SetActiveType(EUpscalerType type) {}
        virtual void OnSwapChainDestroyed() = 0;
        // A frame-generation provider may own the presentation path itself: Streamline installs its
        // DLFG proxy swapchain for as long as the DLSS-G plugin is loaded, and that proxy repaces a
        // FIFO swapchain even with frame generation switched off -- it hands swapchain images back
        // in bursts instead of one per vblank. Ownership can only change while no swapchain is
        // alive, so the renderer calls this from CreateSwapChain, with the device idle, to say
        // whether this swapchain lifetime is going to run frame generation at all.
        virtual void SetFrameGenerationFeatureEnabled(bool /*enabled*/) {}
        virtual void Shutdown() = 0;
        virtual FOptimalRenderSettings GetOptimalRenderSettings(
            uint32_t superResolutionMode,
            VkExtent2D outputExtent,
            bool upscalerEnabled,
            bool hdrOutput,
            EUpscalerType type = EUpscalerType::None) = 0;
        virtual uint32_t JitterPhaseCount() const { return 0; }

        virtual FFrameToken BeginFrame(
            uint32_t frameIndex,
            bool frameGenerationEnabled,
            uint32_t frameLimitFps) = 0;
        virtual void MarkFrame(EFrameMarker marker, const FFrameToken& frameToken) = 0;
        virtual void SetReflexOptions(bool lowLatencyEnabled, uint32_t frameLimitFps) = 0;
        virtual void ReflexSleep(const FFrameToken& frameToken) = 0;

        virtual bool Evaluate(const FFrameInputs& inputs) = 0;
        virtual void TagFrameGeneration(const FFrameInputs& inputs) = 0;
        virtual void UpdateFrameGenerationState() = 0;

        virtual FFrameGenerationState FrameGenerationState() const = 0;
    };
}
