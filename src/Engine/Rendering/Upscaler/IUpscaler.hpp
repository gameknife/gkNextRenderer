#pragma once

#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"

namespace Rendering::Upscaler
{
    class IUpscaler
    {
    public:
        virtual ~IUpscaler() = default;

        virtual void OnDeviceCreated(const FDeviceInfo& deviceInfo, FFeatureCaps& caps) = 0;
        // Called after swapchain selection resolves. Providers must keep GPU
        // resources only while one of their types is active.
        virtual void SetActiveType(EUpscalerType type) {}
        virtual void OnSwapChainDestroyed() = 0;
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
