#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ScadLibrary::AI
{
    struct FScadStudioImportCandidate
    {
        std::string id;
        std::string title;
        std::string activeFilePath;
        std::string source;
        size_t fileCount = 0;
        int64_t updatedAt = 0;
        std::vector<std::string> warnings;
    };

    class FScadStudioSessionImporter
    {
    public:
        static std::vector<FScadStudioImportCandidate> Scan(const std::filesystem::path& workspace,
                                                            std::vector<std::string>& outWarnings);
    };
} // namespace ScadLibrary::AI
