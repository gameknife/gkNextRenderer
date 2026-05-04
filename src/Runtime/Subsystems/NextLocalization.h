#pragma once

#include "Common/CoreMinimal.hpp"

#include <fmt/format.h>
#include <string_view>
#include <unordered_map>

class NextLocalization
{
public:
    bool LoadFromJson(const std::string& path, std::string_view language = "zh");
    bool LoadFromTxt(const std::string& path, std::string_view language);
    bool SaveToTxt(const std::string& path) const;
    void SetLanguage(std::string_view language) { language_ = std::string(language); }
    const std::string& GetLanguage() const { return language_; }

    std::string Get(std::string_view key, std::string_view fallback = {}) const;

    template <typename... Args>
    std::string Format(std::string_view key, std::string_view fallback, Args&&... args) const
    {
        return fmt::format(fmt::runtime(Get(key, fallback)), std::forward<Args>(args)...);
    }

private:
    mutable std::unordered_map<std::string, std::string> translations_;
    std::string language_ = "zh";
};
