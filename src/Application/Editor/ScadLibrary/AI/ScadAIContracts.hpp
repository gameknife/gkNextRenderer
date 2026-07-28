#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ScadLibrary::AI
{
    enum class EScadAIEditKind
    {
        KitModule,
        SceneSource,
        SceneObjects,
        TerrainProcess,
        RigClip,
    };

    enum class EScadAIProposalState
    {
        Idle,
        Generating,
        Validating,
        Ready,
        Stale,
        Applied,
        Rejected,
        Cancelled,
        Error,
    };

    enum class EScadAIValidationSeverity
    {
        Warning,
        Error,
    };

    struct FScadAIEditTarget
    {
        EScadAIEditKind kind = EScadAIEditKind::SceneSource;
        std::string documentKey;
        std::string displayName;
        std::string primaryId;
        std::vector<std::string> secondaryIds;

        bool operator==(const FScadAIEditTarget&) const = default;
    };

    struct FScadDocumentRevision
    {
        uint64_t generation = 0;
        uint64_t contentHash = 0;

        bool operator==(const FScadDocumentRevision&) const = default;
    };

    struct FScadAIValidationIssue
    {
        EScadAIValidationSeverity severity = EScadAIValidationSeverity::Error;
        std::string code;
        std::string message;
    };

    struct FScadAIRequestEnvelope
    {
        std::string requestId;
        FScadAIEditTarget target;
        FScadDocumentRevision baseRevision;
        std::string conversationKey;
        std::string instruction;
        std::string systemPrompt;
        nlohmann::json snapshot;
        std::string schemaName;
        std::string jsonSchema;
        bool strictSchema = true;
    };

    struct FScadAIValidationResult
    {
        bool success = false;
        std::string summary;
        nlohmann::json artifact;
        nlohmann::json candidate;
        std::vector<std::string> semanticDiff;
        std::vector<FScadAIValidationIssue> issues;
    };

    using FScadAIArtifactValidator = std::function<FScadAIValidationResult(std::string_view)>;

    struct FScadAIProposal
    {
        std::string requestId;
        FScadAIEditTarget target;
        FScadDocumentRevision baseRevision;
        EScadAIProposalState state = EScadAIProposalState::Idle;
        std::string summary;
        std::string rawResponse;
        nlohmann::json artifact;
        nlohmann::json candidate;
        std::vector<std::string> semanticDiff;
        std::vector<FScadAIValidationIssue> issues;
        int repairCount = 0;
    };

    uint64_t HashCanonicalSnapshot(const nlohmann::json& snapshot);
    bool IsProposalCurrent(const FScadAIProposal& proposal, const FScadAIEditTarget& target,
                           const FScadDocumentRevision& revision);
    const char* EditKindName(EScadAIEditKind kind);
    const char* ProposalStateName(EScadAIProposalState state);
    std::string MakeScadAIRequestId();

    bool RequireExactObjectKeys(const nlohmann::json& object, std::initializer_list<std::string_view> required,
                                std::initializer_list<std::string_view> optional, std::string& outError);
    bool ReadFiniteNumber(const nlohmann::json& object, std::string_view key, double& outValue,
                          std::string& outError);
} // namespace ScadLibrary::AI
