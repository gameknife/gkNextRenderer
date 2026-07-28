#pragma once

#include "Modules/NextAI/AI/AIChat.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ScadLibrary::AI
{
    struct FScadAIProviderOption
    {
        std::string id;
        std::string displayName;
        std::vector<std::string> models;
        bool configured = false;
        bool available = false;
    };

    struct FScadAITransportConfiguration
    {
        std::vector<FScadAIProviderOption> providers;
        std::string currentProviderId;
        std::string currentModelId;
        std::string statusMessage;
    };

    class IScadAITransport
    {
    public:
        virtual ~IScadAITransport() = default;
        virtual bool LoadConfiguration(FScadAITransportConfiguration& outConfiguration,
                                       std::string& outError) = 0;
        virtual bool SelectProvider(const std::string& providerId,
                                    FScadAITransportConfiguration& outConfiguration,
                                    std::string& outError) = 0;
        virtual bool SelectModel(const std::string& modelId,
                                 FScadAITransportConfiguration& outConfiguration,
                                 std::string& outError) = 0;
        virtual NextAI::FChatResponse Complete(const NextAI::FChatRequest& request,
                                               NextAI::FChatStreamCallback onDelta) = 0;
        virtual bool Cancel(const std::string& runId) = 0;
    };
} // namespace ScadLibrary::AI
