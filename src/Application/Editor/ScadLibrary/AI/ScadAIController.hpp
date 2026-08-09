#pragma once

#include "IScadAITransport.hpp"
#include "ScadAIContracts.hpp"
#include "ScadAIHistoryStore.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace ScadLibrary::AI
{
    struct FScadAIControllerSnapshot
    {
        EScadAIProposalState state = EScadAIProposalState::Idle;
        std::string streamText;
        std::string statusMessage;
        std::optional<FScadAIProposal> proposal;
    };

    class FScadAIController
    {
    public:
        explicit FScadAIController(std::unique_ptr<IScadAITransport> transport,
                                   std::filesystem::path historyRoot = {});
        ~FScadAIController();

        bool Submit(FScadAIRequestEnvelope request, FScadAIArtifactValidator validator);
        void Cancel();
        void Reject();
        void Reset();
        void MarkApplied();
        void RefreshIdentity(const FScadAIEditTarget& target, const FScadDocumentRevision& revision);

        bool LoadTransportConfiguration(FScadAITransportConfiguration& outConfiguration,
                                        std::string& outError);
        bool SelectProvider(const std::string& providerId,
                            FScadAITransportConfiguration& outConfiguration,
                            std::string& outError);
        bool SelectModel(const std::string& modelId,
                         FScadAITransportConfiguration& outConfiguration,
                         std::string& outError);
        bool IsGenerating() const;
        FScadAIControllerSnapshot Snapshot() const;

    private:
        void Run(FScadAIRequestEnvelope request, FScadAIArtifactValidator validator);
        void SetTerminalState(EScadAIProposalState state, std::string message);

        std::unique_ptr<IScadAITransport> transport_;
        mutable std::mutex mutex_;
        std::jthread worker_;
        std::atomic_bool cancelRequested_{false};
        EScadAIProposalState state_ = EScadAIProposalState::Idle;
        std::string activeRequestId_;
        std::string activeRunId_;
        std::string activeConversationKey_;
        std::string activeInstruction_;
        std::string streamText_;
        std::string statusMessage_;
        std::optional<FScadAIProposal> proposal_;
        FScadAIHistoryStore historyStore_;
    };
} // namespace ScadLibrary::AI
