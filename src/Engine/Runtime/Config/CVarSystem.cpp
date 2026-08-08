#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace NextCVar
{
    template <typename StoredT, typename T>
    bool FCVarSystem::RegisterTyped(const std::string& name, T defaultValue, T* target,
                                    ECVarFlags flags, std::string description,
                                    std::function<void()> onChanged,
                                    std::optional<double> minValue,
                                    std::optional<double> maxValue,
                                    bool isUnsigned)
    {
        if (cvars_.contains(name))
        {
            return false;
        }

        FCVarEntry entry{};
        entry.name = name;
        entry.description = std::move(description);
        if constexpr (std::is_same_v<StoredT, int64_t>)
            entry.type = ECVarType::Int;
        else if constexpr (std::is_same_v<StoredT, double>)
            entry.type = ECVarType::Float;
        else if constexpr (std::is_same_v<StoredT, bool>)
            entry.type = ECVarType::Bool;
        else
            entry.type = ECVarType::String;
        entry.flags = flags;
        entry.defaultValue = static_cast<StoredT>(defaultValue);
        entry.value = static_cast<StoredT>(defaultValue);
        entry.minValue = minValue;
        entry.maxValue = maxValue;
        entry.isUnsigned = isUnsigned;
        entry.onChanged = std::move(onChanged);

        if (target)
        {
            entry.boundTarget = target;
            *target = defaultValue;
        }

        cvars_.emplace(name, std::move(entry));
        return true;
    }

    bool FCVarSystem::RegisterInt(const std::string& name, int32_t defaultValue, int32_t* target,
                                  ECVarFlags flags, std::string description,
                                  std::function<void()> onChanged,
                                  std::optional<int64_t> minValue,
                                  std::optional<int64_t> maxValue)
    {
        return RegisterTyped<int64_t>(name, defaultValue, target, flags, std::move(description), std::move(onChanged),
                                      minValue ? std::optional<double>(static_cast<double>(*minValue)) : std::nullopt,
                                      maxValue ? std::optional<double>(static_cast<double>(*maxValue)) : std::nullopt);
    }

    bool FCVarSystem::RegisterUInt(const std::string& name, uint32_t defaultValue, uint32_t* target,
                                   ECVarFlags flags, std::string description,
                                   std::function<void()> onChanged,
                                   std::optional<uint64_t> minValue,
                                   std::optional<uint64_t> maxValue)
    {
        return RegisterTyped<int64_t>(name, defaultValue, target, flags, std::move(description), std::move(onChanged),
                                      minValue ? std::optional<double>(static_cast<double>(*minValue)) : 0.0,
                                      maxValue ? std::optional<double>(static_cast<double>(*maxValue)) : std::nullopt,
                                      true);
    }

    bool FCVarSystem::RegisterFloat(const std::string& name, float defaultValue, float* target,
                                    ECVarFlags flags, std::string description,
                                    std::function<void()> onChanged,
                                    std::optional<double> minValue,
                                    std::optional<double> maxValue)
    {
        return RegisterTyped<double>(name, defaultValue, target, flags, std::move(description), std::move(onChanged),
                                     minValue, maxValue);
    }

    bool FCVarSystem::RegisterBool(const std::string& name, bool defaultValue, bool* target,
                                   ECVarFlags flags, std::string description,
                                   std::function<void()> onChanged)
    {
        return RegisterTyped<bool>(name, defaultValue, target, flags, std::move(description), std::move(onChanged));
    }

    bool FCVarSystem::RegisterString(const std::string& name, std::string defaultValue, std::string* target,
                                     ECVarFlags flags, std::string description,
                                     std::function<void()> onChanged)
    {
        return RegisterTyped<std::string>(name, std::move(defaultValue), target, flags, std::move(description),
                                          std::move(onChanged));
    }

    // Iterates a { "name": value } JSON config; values are passed on as text.
    // User configs live in the per-user writable root, shipped defaults in the
    // read-only runtime root.
    static bool ForEachJsonConfigEntry(const std::string& path,
                                       const std::function<void(const std::string&, const std::string&)>& apply,
                                       const bool userConfig = false)
    {
        const std::string resolvedPath = userConfig
            ? Utilities::FileHelper::ResolveWritableFileForRead(path.c_str())
            : Utilities::FileHelper::GetPlatformFilePath(path.c_str());
        std::ifstream file(resolvedPath);
        if (!file.is_open())
        {
            return false;
        }

        try
        {
            json j;
            file >> j;
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                apply(it.key(), it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
            }
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool FCVarSystem::LoadDefaultFile(const std::string& path)
    {
        defaultConfigPath_ = path;
        return ForEachJsonConfigEntry(path, [this](const std::string& name, const std::string& valueText)
        {
            std::string error;
            (void)SetDefaultFromString(name, valueText, &error);
        });
    }

    bool FCVarSystem::LoadUserFile(const std::string& path)
    {
        userConfigPath_ = path;
        return ForEachJsonConfigEntry(path, [this](const std::string& name, const std::string& valueText)
        {
            std::string error;
            (void)SetValueFromString(name, valueText, ECVarSetBy::UserFile, &error);
        }, true);
    }

    bool FCVarSystem::SaveUserFile(const std::string& path) const
    {
        std::string outputPath = path.empty() ? userConfigPath_ : path;
        if (outputPath.empty())
        {
            return false;
        }

        return SaveEntries(outputPath, [](const std::string&) { return true; });
    }

    void FCVarSystem::RegisterUserFileChannel(std::string prefix, std::string path)
    {
        if (prefix.empty() || path.empty())
        {
            return;
        }

        auto it = std::find_if(userFileChannels_.begin(), userFileChannels_.end(),
                               [&](const auto& channel) { return channel.first == prefix; });
        if (it != userFileChannels_.end())
        {
            it->second = std::move(path);
            return;
        }
        userFileChannels_.emplace_back(std::move(prefix), std::move(path));
        std::sort(userFileChannels_.begin(), userFileChannels_.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first.size() > rhs.first.size(); });
    }

    bool FCVarSystem::LoadUserFiles()
    {
        const std::string sharedPath = userConfigPath_;
        bool loadedAny = LoadUserFile(sharedPath);
        for (const auto& [prefix, path] : userFileChannels_)
        {
            (void)prefix;
            loadedAny = LoadUserFile(path) || loadedAny;
        }
        userConfigPath_ = sharedPath;
        return loadedAny;
    }

    bool FCVarSystem::SaveUserFiles() const
    {
        bool success = SaveEntries(userConfigPath_,
                                   [this](const std::string& name)
                                   {
                                       return ChannelPathForName(name) == userConfigPath_;
                                   });
        for (const auto& [prefix, path] : userFileChannels_)
        {
            success = SaveEntries(path,
                                  [this, &path](const std::string& name)
                                  {
                                      return ChannelPathForName(name) == path;
                                  }) && success;
        }
        return success;
    }

    bool FCVarSystem::SaveEntries(const std::string& outputPath,
                                  const std::function<bool(const std::string&)>& includeName) const
    {
        json j = json::object();

        for (const auto& [name, entry] : cvars_)
        {
            if (!includeName(name) || !HasFlag(entry.flags, ECVarFlags::Archive) || IsDefaultValue(entry))
            {
                continue;
            }

            std::visit([&](const auto& value) { j[name] = value; }, GetEntryValue(entry));
        }

        std::string configPath = Utilities::FileHelper::GetWritableFilePath(outputPath.c_str());
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(configPath).parent_path());

        std::ofstream file(configPath);
        if (!file.is_open())
        {
            return false;
        }

        file << j.dump(2);
        return true;
    }

    std::string FCVarSystem::ChannelPathForName(const std::string& name) const
    {
        for (const auto& [prefix, path] : userFileChannels_)
        {
            if (name.rfind(prefix, 0) == 0)
            {
                return path;
            }
        }
        return userConfigPath_;
    }

    FConsoleResult FCVarSystem::ExecuteCommand(const std::string& line)
    {
        std::string trimmed = Trim(line);
        if (trimmed.empty())
        {
            return FConsoleResult::Failure("Empty command");
        }

        auto tokens = Split(trimmed);
        if (tokens.empty())
        {
            return FConsoleResult::Failure("Empty command");
        }

        auto toggleBoolCVar = [this](const std::string& name) -> FConsoleResult
        {
            auto it = cvars_.find(name);
            if (it == cvars_.end())
            {
                return FConsoleResult::Failure("Unknown cvar");
            }

            if (it->second.type != ECVarType::Bool)
            {
                return FConsoleResult::Failure("toggle only supports bool cvars");
            }

            const bool current = std::get<bool>(GetEntryValue(it->second));
            std::string error;
            if (!SetEntryValue(it->second, !current, ECVarSetBy::Console, &error))
            {
                return FConsoleResult::Failure(error.empty() ? "Failed to toggle" : error);
            }

            return FConsoleResult::Success(
                fmt::format("{} = {}", name, GetValueString(name)));
        };

        if (tokens[0] == "cvar.list")
        {
            std::string prefix = tokens.size() > 1 ? tokens[1] : "";
            auto list = Match(prefix, {.includeValue = true});
            FConsoleResult result = FConsoleResult::Success(fmt::format("{} entries", list.size()));
            result.output = std::move(list);
            return result;
        }

        if (tokens[0] == "cvar.complete")
        {
            std::string query = tokens.size() > 1 ? tokens[1] : "";
            size_t totalMatches = 0;
            auto matches = Match(query, {.prefixThenSubstring = true, .limit = 20}, &totalMatches);

            FConsoleResult result = FConsoleResult::Success(fmt::format("{} matches", totalMatches));
            if (matches.empty())
            {
                result.output.emplace_back("(no matches)");
                return result;
            }

            result.output = std::move(matches);
            if (totalMatches > result.output.size())
            {
                result.output.push_back(fmt::format("... ({} more, refine prefix)",
                                                    totalMatches - result.output.size()));
            }
            return result;
        }

        if (tokens[0] == "cvar.help")
        {
            if (tokens.size() < 2)
            {
                return FConsoleResult::Failure("Usage: cvar.help <name>. Use cvar.complete <prefix> for fuzzy lookup");
            }

            auto it = cvars_.find(tokens[1]);
            if (it == cvars_.end())
            {
                return FConsoleResult::Failure("Unknown cvar");
            }

            const auto& entry = it->second;
            FConsoleResult result = FConsoleResult::Success(entry.name);
            result.output.push_back(fmt::format("Type: {}", entry.type == ECVarType::Int ? "int" :
                entry.type == ECVarType::Float ? "float" : entry.type == ECVarType::Bool ? "bool" : "string"));
            result.output.push_back(fmt::format("Flags: {}", FlagsToString(entry.flags)));
            result.output.push_back(fmt::format("Default: {}", ToString(entry.defaultValue, entry.type)));
            result.output.push_back(fmt::format("Current: {}", ToString(GetEntryValue(entry), entry.type)));
            if (!entry.description.empty())
            {
                result.output.push_back(fmt::format("Desc: {}", entry.description));
            }
            result.output.emplace_back("Use cvar.complete <prefix> for fuzzy lookup");
            return result;
        }

        if (tokens[0] == "cvar.reset")
        {
            if (tokens.size() < 2)
            {
                return FConsoleResult::Failure("Usage: cvar.reset <name>");
            }

            if (ResetToDefault(tokens[1]))
            {
                return FConsoleResult::Success(fmt::format("Reset {}", tokens[1]));
            }
            return FConsoleResult::Failure("Unknown cvar");
        }

        if (tokens[0] == "cvar.toggle")
        {
            if (tokens.size() < 2)
            {
                return FConsoleResult::Failure("Usage: cvar.toggle <name>");
            }
            return toggleBoolCVar(tokens[1]);
        }

        if (tokens[0] == "cvar.editor")
        {
            return toggleBoolCVar("debug.cvar.panel");
        }

        const std::string toggleSuffix = ".toggle";
        if (tokens.size() == 1 &&
            tokens[0].size() > toggleSuffix.size() &&
            tokens[0].compare(tokens[0].size() - toggleSuffix.size(), toggleSuffix.size(), toggleSuffix) == 0)
        {
            const std::string cvarName = tokens[0].substr(0, tokens[0].size() - toggleSuffix.size());
            return toggleBoolCVar(cvarName);
        }

        if (tokens[0] == "cvar.save")
        {
            if (SaveUserFiles())
            {
                return FConsoleResult::Success("Saved user cvar files");
            }
            return FConsoleResult::Failure("Failed to save user cvar files");
        }

        std::string name;
        std::string valueText;
        auto equalPos = trimmed.find('=');
        if (equalPos != std::string::npos)
        {
            name = Trim(trimmed.substr(0, equalPos));
            valueText = Trim(trimmed.substr(equalPos + 1));
        }
        else if (tokens.size() >= 2)
        {
            name = tokens[0];
            valueText = Trim(trimmed.substr(name.size()));
        }
        else
        {
            bool found = false;
            std::string value = GetValueString(trimmed, &found);
            if (!found)
            {
                return FConsoleResult::Failure("Unknown cvar");
            }
            return FConsoleResult::Success(fmt::format("{} = {}", trimmed, value));
        }

        std::string error;
        if (!SetValueFromString(name, valueText, ECVarSetBy::Console, &error))
        {
            return FConsoleResult::Failure(error.empty() ? "Failed to set" : error);
        }

        return FConsoleResult::Success(fmt::format("{} = {}", name, GetValueString(name)));
    }

    bool FCVarSystem::SetValueFromString(const std::string& name, const std::string& value,
                                         ECVarSetBy setBy, std::string* outError)
    {
        auto it = cvars_.find(name);
        if (it == cvars_.end())
        {
            if (outError)
            {
                *outError = "Unknown cvar";
            }
            return false;
        }

        FCVarValue parsedValue;
        if (!ParseValue(value, it->second.type, parsedValue, outError))
        {
            return false;
        }

        return SetEntryValue(it->second, parsedValue, setBy, outError);
    }

    bool FCVarSystem::SetDefaultFromString(const std::string& name, const std::string& value, std::string* outError)
    {
        auto it = cvars_.find(name);
        if (it == cvars_.end())
        {
            if (outError)
            {
                *outError = "Unknown cvar";
            }
            return false;
        }

        FCVarValue parsedValue;
        if (!ParseValue(value, it->second.type, parsedValue, outError))
        {
            return false;
        }

        return ApplyDefaultValue(it->second, parsedValue);
    }

    std::string FCVarSystem::GetValueString(const std::string& name, bool* found) const
    {
        auto it = cvars_.find(name);
        if (it == cvars_.end())
        {
            if (found)
            {
                *found = false;
            }
            return {};
        }

        if (found)
        {
            *found = true;
        }

        return ToString(GetEntryValue(it->second), it->second.type);
    }

    bool FCVarSystem::ResetToDefault(const std::string& name)
    {
        auto it = cvars_.find(name);
        if (it == cvars_.end())
        {
            return false;
        }

        std::string error;
        return SetEntryValue(it->second, it->second.defaultValue, ECVarSetBy::DefaultFile, &error);
    }

    bool FCVarSystem::TryGetInfo(const std::string& name, FCVarInfo& outInfo) const
    {
        const auto it = cvars_.find(name);
        if (it == cvars_.end())
        {
            return false;
        }
        outInfo = MakeInfo(it->second);
        return true;
    }

    void FCVarSystem::ForEach(const std::function<void(const FCVarInfo&)>& fn) const
    {
        std::vector<const FCVarEntry*> entries;
        entries.reserve(cvars_.size());
        for (const auto& [name, entry] : cvars_)
        {
            (void)name;
            entries.push_back(&entry);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const FCVarEntry* lhs, const FCVarEntry* rhs) { return lhs->name < rhs->name; });
        for (const FCVarEntry* entry : entries)
        {
            fn(MakeInfo(*entry));
        }
    }

    std::vector<std::string> FCVarSystem::Match(const std::string& query,
                                                const FCVarMatchOptions& options,
                                                size_t* totalMatches) const
    {
        const std::string loweredQuery = ToLower(query);
        std::vector<std::string> prefixMatches;
        std::vector<std::string> substringMatches;

        for (const auto& [name, entry] : cvars_)
        {
            const std::string loweredName = ToLower(name);
            const bool isPrefixMatch = loweredQuery.empty() || loweredName.rfind(loweredQuery, 0) == 0;
            const bool isSubstringMatch = options.prefixThenSubstring &&
                                          !isPrefixMatch &&
                                          !loweredQuery.empty() &&
                                          loweredName.find(loweredQuery) != std::string::npos;

            if (!isPrefixMatch && !isSubstringMatch)
            {
                continue;
            }

            const std::string value = options.includeValue
                ? fmt::format("{} = {}", name, ToString(GetEntryValue(entry), entry.type))
                : name;
            if (isPrefixMatch)
            {
                prefixMatches.push_back(value);
                continue;
            }
            if (isSubstringMatch)
            {
                substringMatches.push_back(value);
            }
        }

        std::sort(prefixMatches.begin(), prefixMatches.end());
        std::sort(substringMatches.begin(), substringMatches.end());

        std::vector<std::string> result = std::move(prefixMatches);
        if (result.empty() && options.prefixThenSubstring)
        {
            result = std::move(substringMatches);
        }

        if (totalMatches)
        {
            *totalMatches = result.size();
        }
        if (options.limit > 0 && result.size() > options.limit)
        {
            result.resize(options.limit);
        }
        return result;
    }

    std::string FCVarSystem::Trim(const std::string& input)
    {
        auto start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return {};
        }
        auto end = input.find_last_not_of(" \t\r\n");
        return input.substr(start, end - start + 1);
    }

    std::vector<std::string> FCVarSystem::Split(const std::string& input)
    {
        std::istringstream stream(input);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token)
        {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string FCVarSystem::ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string FCVarSystem::Unquote(const std::string& value)
    {
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
        {
            return value.substr(1, value.size() - 2);
        }
        return value;
    }

    bool FCVarSystem::ApplyDefaultValue(FCVarEntry& entry, const FCVarValue& value)
    {
        entry.defaultValue = value;
        std::string error;
        return SetEntryValue(entry, value, ECVarSetBy::DefaultFile, &error);
    }

    bool FCVarSystem::SetEntryValue(FCVarEntry& entry, const FCVarValue& value, ECVarSetBy setBy,
                                    std::string* outError)
    {
        if (HasFlag(entry.flags, ECVarFlags::ReadOnly) &&
            (setBy == ECVarSetBy::UserFile || setBy == ECVarSetBy::CommandLine || setBy == ECVarSetBy::Console))
        {
            if (outError)
            {
                *outError = "Read-only cvar";
            }
            return false;
        }

        if (HasFlag(entry.flags, ECVarFlags::StartupOnly) && setBy == ECVarSetBy::Console)
        {
            if (outError)
            {
                *outError = "Startup-only cvar";
            }
            return false;
        }

        FCVarValue normalizedValue = value;
        if (entry.type == ECVarType::Int)
        {
            int64_t val = std::get<int64_t>(normalizedValue);
            if (entry.isUnsigned)
            {
                val = std::max<int64_t>(0, val);
            }
            if (entry.minValue)
            {
                val = std::max(val, static_cast<int64_t>(*entry.minValue));
            }
            if (entry.maxValue)
            {
                val = std::min(val, static_cast<int64_t>(*entry.maxValue));
            }
            normalizedValue = val;
        }
        else if (entry.type == ECVarType::Float)
        {
            double val = std::get<double>(normalizedValue);
            if (entry.minValue)
            {
                val = std::max(val, *entry.minValue);
            }
            if (entry.maxValue)
            {
                val = std::min(val, *entry.maxValue);
            }
            normalizedValue = static_cast<double>(static_cast<float>(val));
        }

        const FCVarValue previousValue = GetEntryValue(entry);
        if (previousValue == normalizedValue)
        {
            return true;
        }

        entry.setBy = setBy;

        // Write through to the bound target (unbound cvars store into entry.value).
        // The variant alternative implies the required entry type.
        const bool applied = std::visit(
            [&]<typename TargetPtr>(TargetPtr target) -> bool
            {
                if constexpr (std::is_same_v<TargetPtr, std::monostate>)
                {
                    entry.value = normalizedValue;
                    return true;
                }
                else
                {
                    using TargetT = std::remove_pointer_t<TargetPtr>;
                    constexpr ECVarType requiredType =
                        std::is_same_v<TargetT, float>       ? ECVarType::Float
                        : std::is_same_v<TargetT, bool>        ? ECVarType::Bool
                        : std::is_same_v<TargetT, std::string> ? ECVarType::String
                                                               : ECVarType::Int;
                    if (entry.type != requiredType)
                    {
                        return false;
                    }
                    if constexpr (std::is_same_v<TargetT, int32_t> || std::is_same_v<TargetT, uint32_t>)
                    {
                        int64_t val = std::get<int64_t>(normalizedValue);
                        if (std::is_same_v<TargetT, uint32_t> && val < 0)
                        {
                            val = 0;
                        }
                        *target = static_cast<TargetT>(val);
                    }
                    else if constexpr (std::is_same_v<TargetT, float>)
                    {
                        *target = static_cast<float>(std::get<double>(normalizedValue));
                    }
                    else
                    {
                        *target = std::get<TargetT>(normalizedValue);
                    }
                    return true;
                }
            },
            entry.boundTarget);
        if (!applied)
        {
            return false;
        }

        if (entry.onChanged)
        {
            entry.onChanged();
        }

        return true;
    }

    FCVarSystem::FCVarValue FCVarSystem::GetEntryValue(const FCVarEntry& entry) const
    {
        return std::visit(
            [&]<typename TargetPtr>(TargetPtr target) -> FCVarValue
            {
                if constexpr (std::is_same_v<TargetPtr, std::monostate>)
                {
                    return entry.value;
                }
                else if constexpr (std::is_same_v<TargetPtr, int32_t*> || std::is_same_v<TargetPtr, uint32_t*>)
                {
                    return static_cast<int64_t>(*target);
                }
                else if constexpr (std::is_same_v<TargetPtr, float*>)
                {
                    return static_cast<double>(*target);
                }
                else
                {
                    return *target;
                }
            },
            entry.boundTarget);
    }

    FCVarInfo FCVarSystem::MakeInfo(const FCVarEntry& entry) const
    {
        return {
            .name = entry.name,
            .description = entry.description,
            .type = entry.type,
            .flags = entry.flags,
            .isDefault = IsDefaultValue(entry),
            .isUnsigned = entry.isUnsigned,
            .minValue = entry.minValue,
            .maxValue = entry.maxValue,
        };
    }

    bool FCVarSystem::IsDefaultValue(const FCVarEntry& entry) const
    {
        const FCVarValue currentValue = GetEntryValue(entry);
        if (entry.type == ECVarType::Float)
        {
            const double current = std::get<double>(currentValue);
            const double def = std::get<double>(entry.defaultValue);
            const double scale = std::max({1.0, std::fabs(current), std::fabs(def)});
            return std::fabs(current - def) <= 0.0001 * scale;
        }
        return currentValue == entry.defaultValue;
    }

    std::string FCVarSystem::ToString(const FCVarValue& value, ECVarType type) const
    {
        switch (type)
        {
        case ECVarType::Int:
            return fmt::format("{}", std::get<int64_t>(value));
        case ECVarType::Float:
            return fmt::format("{:.4f}", std::get<double>(value));
        case ECVarType::Bool:
            return std::get<bool>(value) ? "true" : "false";
        case ECVarType::String:
            return std::get<std::string>(value);
        default:
            return {};
        }
    }

    std::string FCVarSystem::FlagsToString(ECVarFlags flags) const
    {
        if (flags == ECVarFlags::None)
        {
            return "None";
        }

        std::vector<std::string> parts;
        if (HasFlag(flags, ECVarFlags::ReadOnly))
            parts.emplace_back("ReadOnly");
        if (HasFlag(flags, ECVarFlags::Archive))
            parts.emplace_back("Archive");
        if (HasFlag(flags, ECVarFlags::StartupOnly))
            parts.emplace_back("StartupOnly");

        return fmt::format("{}", fmt::join(parts, "|"));
    }

    bool FCVarSystem::ParseValue(const std::string& valueText, ECVarType type, FCVarValue& outValue,
                                 std::string* outError) const
    {
        try
        {
            switch (type)
            {
            case ECVarType::Int:
                {
                    std::string trimmed = Trim(valueText);
                    size_t idx = 0;
                    int64_t value = std::stoll(trimmed, &idx, 0);
                    if (idx != trimmed.size())
                    {
                        throw std::invalid_argument("invalid int");
                    }
                    outValue = value;
                    return true;
                }
            case ECVarType::Float:
                {
                    std::string trimmed = Trim(valueText);
                    size_t idx = 0;
                    double value = std::stod(trimmed, &idx);
                    if (idx != trimmed.size())
                    {
                        throw std::invalid_argument("invalid float");
                    }
                    outValue = value;
                    return true;
                }
            case ECVarType::Bool:
                {
                    std::string lowered = ToLower(Trim(valueText));
                    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes")
                    {
                        outValue = true;
                        return true;
                    }
                    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no")
                    {
                        outValue = false;
                        return true;
                    }
                    throw std::invalid_argument("invalid bool");
                }
            case ECVarType::String:
                outValue = Unquote(Trim(valueText));
                return true;
            default:
                break;
            }
        }
        catch (const std::exception&)
        {
            if (outError)
            {
                *outError = "Invalid value";
            }
        }

        return false;
    }
}
