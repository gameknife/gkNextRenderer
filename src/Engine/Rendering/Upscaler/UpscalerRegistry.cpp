#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"

namespace Rendering::Upscaler
{
    namespace
    {
        std::vector<FUpscalerFactory>& Factories()
        {
            static std::vector<FUpscalerFactory> factories;
            return factories;
        }

        class FCompositeUpscaler final : public IUpscaler
        {
        public:
            explicit FCompositeUpscaler(std::vector<std::unique_ptr<IUpscaler>> providers) :
                providers_(std::move(providers)), providerCaps_(providers_.size())
            {
            }

            void OnDeviceCreated(const FDeviceInfo& deviceInfo, FFeatureCaps& caps) override
            {
                caps = {};
                for (size_t i = 0; i < providers_.size(); ++i)
                {
                    providers_[i]->OnDeviceCreated(deviceInfo, providerCaps_[i]);
                    const auto& providerCaps = providerCaps_[i];
                    caps.streamlineInitialized |= providerCaps.streamlineInitialized;
                    caps.streamlineDeviceReady |= providerCaps.streamlineDeviceReady;
                    caps.supportDLSS |= providerCaps.supportDLSS;
                    caps.supportDLSSRR |= providerCaps.supportDLSSRR;
                    caps.supportDLSSG |= providerCaps.supportDLSSG;
                    caps.supportReflex |= providerCaps.supportReflex;
                    caps.supportPCL |= providerCaps.supportPCL;
                    caps.supportFSR |= providerCaps.supportFSR;
                    caps.supportFSRFrameGeneration |= providerCaps.supportFSRFrameGeneration;
                    caps.supportNativeTemporal |= providerCaps.supportNativeTemporal;
                    caps.requestedDeviceExtensionsAvailable |= providerCaps.requestedDeviceExtensionsAvailable;
                    caps.requiredGraphicsQueues = std::max(caps.requiredGraphicsQueues, providerCaps.requiredGraphicsQueues);
                    caps.requiredComputeQueues = std::max(caps.requiredComputeQueues, providerCaps.requiredComputeQueues);
                    caps.requiredOpticalFlowQueues = std::max(caps.requiredOpticalFlowQueues, providerCaps.requiredOpticalFlowQueues);
                }
            }

            void OnSwapChainDestroyed() override
            {
                for (auto& provider : providers_)
                {
                    provider->OnSwapChainDestroyed();
                }
                activeProvider_ = nullptr;
            }

            void Shutdown() override
            {
                for (auto& provider : providers_)
                {
                    provider->Shutdown();
                }
            }

            FOptimalRenderSettings GetOptimalRenderSettings(
                uint32_t mode, VkExtent2D outputExtent, bool enabled, bool hdrOutput,
                EUpscalerProvider requestedProvider) override
            {
                activeProvider_ = FindProvider(requestedProvider);
                if (activeProvider_ != nullptr)
                {
                    return activeProvider_->GetOptimalRenderSettings(
                        mode, outputExtent, enabled, hdrOutput, requestedProvider);
                }

                const VkExtent2D renderExtent = ScaleExtent(
                    outputExtent, GetUpscaleModeInfo(mode).fallbackScale);
                return {renderExtent, renderExtent, outputExtent, false};
            }

            uint32_t JitterPhaseCount() const override
            {
                return activeProvider_ != nullptr ? activeProvider_->JitterPhaseCount() : 0;
            }

            FFrameToken BeginFrame(
                uint32_t frameIndex, bool frameGenerationEnabled, uint32_t frameLimitFps) override
            {
                FFrameToken result{};
                for (auto& provider : providers_)
                {
                    const FFrameToken token = provider->BeginFrame(
                        frameIndex, frameGenerationEnabled, frameLimitFps);
                    if (!result && token)
                    {
                        result = token;
                    }
                }
                return result;
            }

            void MarkFrame(EFrameMarker marker, const FFrameToken& token) override
            {
                for (auto& provider : providers_)
                {
                    provider->MarkFrame(marker, token);
                }
            }

            void SetReflexOptions(bool enabled, uint32_t frameLimitFps) override
            {
                for (auto& provider : providers_)
                {
                    provider->SetReflexOptions(enabled, frameLimitFps);
                }
            }

            void ReflexSleep(const FFrameToken& token) override
            {
                for (auto& provider : providers_)
                {
                    provider->ReflexSleep(token);
                }
            }

            bool Evaluate(const FFrameInputs& inputs) override
            {
                IUpscaler* requested = inputs.enableNativeTemporal
                    ? FindProvider(EUpscalerProvider::NativeTemporal)
                    : inputs.enableFSR
                        ? FindProvider(EUpscalerProvider::FidelityFX)
                        : inputs.enableDLSS
                            ? FindProvider(EUpscalerProvider::Streamline)
                            : nullptr;
                return requested != nullptr && requested->Evaluate(inputs);
            }

            void TagFrameGeneration(const FFrameInputs& inputs) override
            {
                for (auto& provider : providers_)
                {
                    provider->TagFrameGeneration(inputs);
                }
            }

            void UpdateFrameGenerationState() override
            {
                for (auto& provider : providers_)
                {
                    provider->UpdateFrameGenerationState();
                }
            }

            FFrameGenerationState FrameGenerationState() const override
            {
                for (const auto& provider : providers_)
                {
                    const FFrameGenerationState state = provider->FrameGenerationState();
                    if (state.valid)
                    {
                        return state;
                    }
                }
                return {};
            }

        private:
            IUpscaler* FindProvider(EUpscalerProvider requestedProvider) const
            {
                for (size_t i = 0; i < providers_.size(); ++i)
                {
                    if (providerCaps_[i].provider == requestedProvider)
                    {
                        return providers_[i].get();
                    }
                }
                return nullptr;
            }

            std::vector<std::unique_ptr<IUpscaler>> providers_;
            std::vector<FFeatureCaps> providerCaps_;
            IUpscaler* activeProvider_ = nullptr;
        };
    }

    void RegisterUpscalerFactory(FUpscalerFactory factory)
    {
        Factories().push_back(std::move(factory));
    }

    bool HasRegisteredUpscalerFactory()
    {
        return !Factories().empty();
    }

    std::unique_ptr<IUpscaler> CreateRegisteredUpscaler()
    {
        std::vector<std::unique_ptr<IUpscaler>> providers;
        for (auto& factory : Factories())
        {
            if (auto provider = factory())
            {
                providers.push_back(std::move(provider));
            }
        }
        if (providers.empty())
        {
            return nullptr;
        }
        return std::make_unique<FCompositeUpscaler>(std::move(providers));
    }
}
