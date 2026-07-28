#pragma once

#include "IScadAITransport.hpp"

#include <memory>

namespace NextAI
{
    class FAIService;
}

namespace ScadLibrary::AI
{
    class FNextAIScadTransport final : public IScadAITransport
    {
    public:
        FNextAIScadTransport();
        ~FNextAIScadTransport() override;

        bool LoadConfiguration(FScadAITransportConfiguration& outConfiguration,
                               std::string& outError) override;
        bool SelectProvider(const std::string& providerId,
                            FScadAITransportConfiguration& outConfiguration,
                            std::string& outError) override;
        bool SelectModel(const std::string& modelId,
                         FScadAITransportConfiguration& outConfiguration,
                         std::string& outError) override;
        NextAI::FChatResponse Complete(const NextAI::FChatRequest& request,
                                       NextAI::FChatStreamCallback onDelta) override;
        bool Cancel(const std::string& runId) override;

        bool IsReady() const;
        std::string StatusMessage() const;

    private:
        bool EnsureService();
        void FillConfiguration(FScadAITransportConfiguration& outConfiguration) const;

        std::unique_ptr<NextAI::FAIService> service_;
    };
} // namespace ScadLibrary::AI
