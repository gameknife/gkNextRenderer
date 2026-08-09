#pragma once

#include "../ScadAIContracts.hpp"

namespace ScadLibrary::AI
{
    class FTerrainProcessAIAdapter
    {
    public:
        static FScadAIRequestEnvelope BuildRequest(const FScadAIEditTarget& target,
                                                   const FScadDocumentRevision& revision,
                                                   const nlohmann::json& snapshot, std::string instruction);
        static FScadAIValidationResult Validate(const nlohmann::json& snapshot, std::string_view response);
    };
} // namespace ScadLibrary::AI
