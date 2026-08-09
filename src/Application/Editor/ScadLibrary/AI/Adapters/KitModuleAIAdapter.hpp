#pragma once

#include "../ScadAIContracts.hpp"

#include <string>

namespace ScadLibrary::AI
{
    class FKitModuleAIAdapter
    {
    public:
        static FScadAIRequestEnvelope BuildRequest(const FScadAIEditTarget& target,
                                                   const FScadDocumentRevision& revision,
                                                   const std::string& kitSource, std::string instruction);
        static FScadAIValidationResult Validate(const std::string& kitSource, const std::string& moduleName,
                                                std::string_view response);
    };
} // namespace ScadLibrary::AI
