#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ScadLibrary::AI
{
    struct FScadAIHistoryEntry
    {
        std::string role;
        std::string text;
        std::string outcome;
    };

    class FScadAIHistoryStore
    {
    public:
        explicit FScadAIHistoryStore(std::filesystem::path root = {});

        std::vector<FScadAIHistoryEntry> Load(const std::string& conversationKey) const;
        bool Append(const std::string& conversationKey, const FScadAIHistoryEntry& entry, std::string& outError);

    private:
        std::filesystem::path ConversationPath(const std::string& conversationKey) const;
        std::filesystem::path root_;
    };
} // namespace ScadLibrary::AI
