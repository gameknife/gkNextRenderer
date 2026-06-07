#include "ScadPromptContext.hpp"

#include <sstream>

namespace ScadStudio
{
    namespace
    {
        std::string ProjectToPrompt(const std::vector<FScadProjectFile>& files, const std::string& currentSource)
        {
            if (files.empty())
            {
                return currentSource.empty() ? std::string()
                                             : "Current single-file model:\n```scad\n" + currentSource + "\n```";
            }

            std::string out = "Current multi-file project (authoritative):\n```scad-project\n";
            for (const FScadProjectFile& file : files)
            {
                out += "--- file: " + file.path + "\n";
                out += file.source;
                if (!out.empty() && out.back() != '\n')
                {
                    out += "\n";
                }
            }
            out += "```";
            return out;
        }
    }

    std::string BuildScadUserPrompt(
        const std::string& currentSource,
        const std::vector<FScadProjectFile>& files,
        const FScadEditScope& editScope,
        const std::string& instruction)
    {
        std::ostringstream prompt;
        const std::string project = ProjectToPrompt(files, currentSource);
        if (!project.empty())
        {
            prompt << project << "\n\n";
        }
        else
        {
            prompt << "Create a new model.\n\n";
        }

        if (editScope.HasFocusedModule())
        {
            prompt << "Current focused module preview:\n";
            prompt << "- Module: " << editScope.focusedModuleName << "\n";
            if (!editScope.EffectiveFilePath().empty())
            {
                prompt << "- File: " << editScope.EffectiveFilePath() << "\n";
            }
            prompt << "Treat this focused module as the DEFAULT edit target for this request. "
                      "Keep unrelated files and modules unchanged unless the user explicitly asks "
                      "for a broader or global edit.\n";
            if (!editScope.EffectiveFilePath().empty())
            {
                prompt << "If you reply with a single ```scad block instead of a full "
                          "```scad-project block, return the COMPLETE contents of "
                       << editScope.EffectiveFilePath()
                       << ", not only the focused module snippet.\n";
            }
            else
            {
                prompt << "If you reply with a single ```scad block, return the COMPLETE "
                          "single-file model, not only the focused module snippet.\n";
            }
        }
        else if (!editScope.activeFilePath.empty())
        {
            prompt << "Preferred file to edit: " << editScope.activeFilePath << "\n";
        }

        prompt << "\nUser request:\n" << instruction;
        return prompt.str();
    }
}
