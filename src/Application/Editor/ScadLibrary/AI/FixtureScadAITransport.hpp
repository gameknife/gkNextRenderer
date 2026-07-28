#pragma once

#include "IScadAITransport.hpp"

namespace ScadLibrary::AI
{
    // Deterministic transport used only by --agent-validation. It echoes a
    // schema-valid, no-op/minimal proposal and never contacts a provider.
    class FFixtureScadAITransport final : public IScadAITransport
    {
    public:
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
    };
} // namespace ScadLibrary::AI
