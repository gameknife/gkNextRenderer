#include "Engine/Common/CoreMinimal.hpp"
#include "ScadAIContracts.hpp"

#include <atomic>
#include <cmath>
#include <unordered_set>

namespace ScadLibrary::AI
{
    uint64_t HashCanonicalSnapshot(const nlohmann::json& snapshot)
    {
        const std::string canonical = snapshot.dump();
        uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : canonical)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool IsProposalCurrent(const FScadAIProposal& proposal, const FScadAIEditTarget& target,
                           const FScadDocumentRevision& revision)
    {
        return proposal.target == target && proposal.baseRevision == revision;
    }

    const char* EditKindName(EScadAIEditKind kind)
    {
        switch (kind)
        {
        case EScadAIEditKind::KitModule: return "Kit";
        case EScadAIEditKind::SceneSource: return "Scene Source";
        case EScadAIEditKind::SceneObjects: return "Scene Objects";
        case EScadAIEditKind::TerrainProcess: return "Terrain Process";
        case EScadAIEditKind::RigClip: return "Rig Clip";
        }
        return "Unknown";
    }

    const char* ProposalStateName(EScadAIProposalState state)
    {
        switch (state)
        {
        case EScadAIProposalState::Idle: return "Idle";
        case EScadAIProposalState::Generating: return "Generating";
        case EScadAIProposalState::Validating: return "Validating";
        case EScadAIProposalState::Ready: return "Ready";
        case EScadAIProposalState::Stale: return "Stale";
        case EScadAIProposalState::Applied: return "Applied";
        case EScadAIProposalState::Rejected: return "Rejected";
        case EScadAIProposalState::Cancelled: return "Cancelled";
        case EScadAIProposalState::Error: return "Error";
        }
        return "Unknown";
    }

    std::string MakeScadAIRequestId()
    {
        static std::atomic<uint64_t> nextId{1};
        return fmt::format("scad-authoring-{}", nextId++);
    }

    bool RequireExactObjectKeys(const nlohmann::json& object, std::initializer_list<std::string_view> required,
                                std::initializer_list<std::string_view> optional, std::string& outError)
    {
        if (!object.is_object())
        {
            outError = "expected a JSON object";
            return false;
        }
        std::unordered_set<std::string> allowed;
        for (const std::string_view key : required)
        {
            allowed.emplace(key);
            if (!object.contains(key))
            {
                outError = fmt::format("missing required field '{}'", key);
                return false;
            }
        }
        for (const std::string_view key : optional)
        {
            allowed.emplace(key);
        }
        for (const auto& [key, value] : object.items())
        {
            (void)value;
            if (!allowed.contains(key))
            {
                outError = fmt::format("unknown field '{}'", key);
                return false;
            }
        }
        outError.clear();
        return true;
    }

    bool ReadFiniteNumber(const nlohmann::json& object, std::string_view key, double& outValue,
                          std::string& outError)
    {
        const auto found = object.find(key);
        if (found == object.end() || !found->is_number())
        {
            outError = fmt::format("field '{}' must be a number", key);
            return false;
        }
        outValue = found->get<double>();
        if (!std::isfinite(outValue))
        {
            outError = fmt::format("field '{}' must be finite", key);
            return false;
        }
        return true;
    }
} // namespace ScadLibrary::AI
