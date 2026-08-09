#pragma once

#include "../ScadAIContracts.hpp"

#include <string>

namespace ScadLibrary::AI
{
    class FSceneSourceAIAdapter
    {
    public:
        static FScadAIRequestEnvelope BuildRequest(const FScadAIEditTarget& target,
                                                   const FScadDocumentRevision& revision,
                                                   const std::string& source, std::string instruction);
        static FScadAIValidationResult Validate(const std::string& baseSource, std::string_view response);
    };
} // namespace ScadLibrary::AI
