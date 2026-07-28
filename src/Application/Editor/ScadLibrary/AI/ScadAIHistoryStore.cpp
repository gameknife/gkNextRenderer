#include "Engine/Common/CoreMinimal.hpp"
#include "ScadAIHistoryStore.hpp"

#include "Engine/Runtime/Platform/UserPaths.hpp"
#include "ScadAIContracts.hpp"
#include "ScadAIValidationPolicy.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ScadLibrary::AI
{
    FScadAIHistoryStore::FScadAIHistoryStore(std::filesystem::path root) :
        root_(root.empty() ? NextPlatform::UserPaths::GetUserDataDir("ScadLibrary") / "ai" : std::move(root))
    {
    }

    std::filesystem::path FScadAIHistoryStore::ConversationPath(const std::string& conversationKey) const
    {
        const uint64_t hash = HashCanonicalSnapshot(conversationKey);
        return root_ / fmt::format("{:016x}.json", hash);
    }

    std::vector<FScadAIHistoryEntry> FScadAIHistoryStore::Load(const std::string& conversationKey) const
    {
        std::ifstream input(ConversationPath(conversationKey), std::ios::binary);
        if (!input)
        {
            return {};
        }
        try
        {
            const nlohmann::json data = nlohmann::json::parse(input);
            std::vector<FScadAIHistoryEntry> entries;
            for (const auto& item : data.value("entries", nlohmann::json::array()))
            {
                if (!item.is_object())
                {
                    continue;
                }
                entries.push_back(
                    {item.value("role", ""), item.value("text", ""), item.value("outcome", "")});
            }
            if (entries.size() > FScadAIValidationPolicy::maxHistoryMessages)
            {
                entries.erase(entries.begin(),
                              entries.end() - static_cast<std::ptrdiff_t>(FScadAIValidationPolicy::maxHistoryMessages));
            }
            return entries;
        }
        catch (...)
        {
            return {};
        }
    }

    bool FScadAIHistoryStore::Append(const std::string& conversationKey, const FScadAIHistoryEntry& entry,
                                     std::string& outError)
    {
        std::vector<FScadAIHistoryEntry> entries = Load(conversationKey);
        entries.push_back(entry);
        if (entries.size() > FScadAIValidationPolicy::maxHistoryMessages)
        {
            entries.erase(entries.begin());
        }
        nlohmann::json data;
        data["version"] = 1;
        data["conversationKey"] = conversationKey;
        data["entries"] = nlohmann::json::array();
        for (const FScadAIHistoryEntry& item : entries)
        {
            data["entries"].push_back({{"role", item.role}, {"text", item.text}, {"outcome", item.outcome}});
        }
        std::error_code error;
        std::filesystem::create_directories(root_, error);
        const std::filesystem::path path = ConversationPath(conversationKey);
        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                outError = "无法写入 AI 历史临时文件";
                return false;
            }
            output << data.dump(2);
        }
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
        if (error)
        {
            outError = error.message();
            return false;
        }
        outError.clear();
        return true;
    }
} // namespace ScadLibrary::AI
