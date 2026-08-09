#include "Engine/Common/CoreMinimal.hpp"
#include "NextAIScadTransport.hpp"

#include "Modules/NextAI/AIService.hpp"

namespace ScadLibrary::AI
{
    FNextAIScadTransport::FNextAIScadTransport() = default;
    FNextAIScadTransport::~FNextAIScadTransport() = default;

    void FNextAIScadTransport::FillConfiguration(FScadAITransportConfiguration& outConfiguration) const
    {
        outConfiguration = {};
        if (!service_)
        {
            return;
        }
        outConfiguration.currentProviderId = service_->GetProviderId();
        outConfiguration.currentModelId = service_->GetCurrentModel();
        outConfiguration.statusMessage = service_->GetStatusMessage();
        for (const NextAI::FAIProviderDescriptor& provider : service_->GetAvailableProviders())
        {
            outConfiguration.providers.push_back(
                {provider.id, provider.displayName, provider.models, provider.configured, provider.available});
        }
    }

    bool FNextAIScadTransport::EnsureService()
    {
        if (!service_)
        {
            service_ = std::make_unique<NextAI::FAIService>();
            if (service_->IsConfigured())
            {
                service_->SetProfile("scad-authoring");
            }
        }
        return service_->IsConfigured();
    }

    bool FNextAIScadTransport::LoadConfiguration(FScadAITransportConfiguration& outConfiguration,
                                                  std::string& outError)
    {
        if (!EnsureService())
        {
            FillConfiguration(outConfiguration);
            outError = StatusMessage();
            return false;
        }
        FillConfiguration(outConfiguration);
        outError.clear();
        return true;
    }

    bool FNextAIScadTransport::SelectProvider(const std::string& providerId,
                                               FScadAITransportConfiguration& outConfiguration,
                                               std::string& outError)
    {
        if (!EnsureService() || !service_->SwitchProvider(providerId))
        {
            FillConfiguration(outConfiguration);
            outError = service_ && !service_->IsProviderConfigured(providerId)
                ? "所选 Provider 未配置或当前不可用"
                : StatusMessage();
            return false;
        }
        FillConfiguration(outConfiguration);
        outError.clear();
        return true;
    }

    bool FNextAIScadTransport::SelectModel(const std::string& modelId,
                                            FScadAITransportConfiguration& outConfiguration,
                                            std::string& outError)
    {
        if (!EnsureService())
        {
            FillConfiguration(outConfiguration);
            outError = StatusMessage();
            return false;
        }
        const std::vector<std::string> models = service_->GetProviderModels(service_->GetProviderId());
        if (std::find(models.begin(), models.end(), modelId) == models.end())
        {
            FillConfiguration(outConfiguration);
            outError = "所选模型不属于当前 Provider";
            return false;
        }
        if (!service_->SetCurrentModel(modelId))
        {
            FillConfiguration(outConfiguration);
            outError = StatusMessage();
            return false;
        }
        FillConfiguration(outConfiguration);
        outError.clear();
        return true;
    }

    NextAI::FChatResponse FNextAIScadTransport::Complete(const NextAI::FChatRequest& request,
                                                         NextAI::FChatStreamCallback onDelta)
    {
        if (!EnsureService())
        {
            return NextAI::FChatResponse::Failure(StatusMessage());
        }
        return service_->ChatStream(request, std::move(onDelta));
    }

    bool FNextAIScadTransport::Cancel(const std::string& runId)
    {
        return service_ && service_->Cancel(runId);
    }

    bool FNextAIScadTransport::IsReady() const
    {
        return service_ && service_->IsConfigured();
    }

    std::string FNextAIScadTransport::StatusMessage() const
    {
        return service_ ? service_->GetStatusMessage() : "AI 尚未初始化";
    }
} // namespace ScadLibrary::AI
