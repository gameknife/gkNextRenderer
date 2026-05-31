#pragma once

#include "ScadSession.hpp"

#include <string>
#include <vector>

namespace ScadStudio
{
    struct FScadEditScope
    {
        std::string activeFilePath;
        std::string focusedModuleName;
        std::string focusedModuleFilePath;

        [[nodiscard]] bool HasFocusedModule() const
        {
            return !focusedModuleName.empty();
        }

        [[nodiscard]] std::string EffectiveFilePath() const
        {
            return focusedModuleFilePath.empty() ? activeFilePath : focusedModuleFilePath;
        }
    };

    std::string BuildScadUserPrompt(
        const std::string& currentSource,
        const std::vector<FScadProjectFile>& files,
        const FScadEditScope& editScope,
        const std::string& instruction);
}
