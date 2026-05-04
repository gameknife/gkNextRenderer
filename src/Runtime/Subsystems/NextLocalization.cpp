#include "Runtime/Subsystems/NextLocalization.h"

#include "Runtime/Utilities/JsonHelpers.h"
#include "Utilities/FileHelper.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <spdlog/spdlog.h>

namespace
{
    std::string TrimLine(const std::string& input)
    {
        const auto start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return {};
        }

        const auto end = input.find_last_not_of(" \t\r\n");
        return input.substr(start, end - start + 1);
    }
}

bool NextLocalization::LoadFromJson(const std::string& path, std::string_view language)
{
    nlohmann::json document;
    if (!NextJson::TryLoadFile(path, document))
    {
        return false;
    }

    const std::string languageKey(language);
    const nlohmann::json* localizedObject = &document;
    if (document.is_object() && document.contains(languageKey) && document.at(languageKey).is_object())
    {
        localizedObject = &document.at(languageKey);
    }

    if (!localizedObject->is_object())
    {
        SPDLOG_ERROR("[NextLocalization] {} does not contain an object for language '{}'", path, languageKey);
        return false;
    }

    SetLanguage(language);
    for (const auto& [key, value] : localizedObject->items())
    {
        if (value.is_string())
        {
            translations_[key] = value.get<std::string>();
        }
    }
    return true;
}

bool NextLocalization::LoadFromTxt(const std::string& path, std::string_view language)
{
    const std::string absolutePath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
    std::ifstream input(absolutePath, std::ios::binary);
    if (!input.is_open())
    {
        SPDLOG_WARN("[NextLocalization] Failed to open locale file: {}", absolutePath);
        return false;
    }

    SetLanguage(language);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line))
    {
        line = TrimLine(line);
        if (line.empty())
        {
            continue;
        }

        const size_t separatorPos = line.find(';');
        if (separatorPos == std::string::npos)
        {
            continue;
        }

        translations_[line.substr(0, separatorPos)] = line.substr(separatorPos + 1);
    }
    return true;
}

bool NextLocalization::SaveToTxt(const std::string& path) const
{
    const std::string normalizedPath = Utilities::FileHelper::GetNormalizedFilePath(path.c_str());
    std::ofstream output(normalizedPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        SPDLOG_WARN("[NextLocalization] Failed to write locale file: {}", normalizedPath);
        return false;
    }

    std::vector<std::pair<std::string, std::string>> entries(translations_.begin(), translations_.end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    for (const auto& [key, value] : entries)
    {
        output << key << ';' << value << '\n';
    }
    return true;
}

std::string NextLocalization::Get(std::string_view key, std::string_view fallback) const
{
    const std::string keyString(key);
    const auto it = translations_.find(keyString);
    if (it != translations_.end())
    {
        if (!it->second.empty())
        {
            return it->second;
        }
        return fallback.empty() ? keyString : std::string(fallback);
    }

    translations_.emplace(keyString, "");
    return fallback.empty() ? keyString : std::string(fallback);
}
