#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include <variant>

namespace NextCVar
{
    enum class ECVarType
    {
        Int,
        Float,
        Bool,
        String
    };

    enum class ECVarFlags : uint32_t
    {
        None = 0,
        ReadOnly = 1 << 0,
        Archive = 1 << 1,
        StartupOnly = 1 << 2
    };

    enum class ECVarSetBy
    {
        Default = 0,
        DefaultFile,
        UserFile,
        CommandLine,
        Console
    };

    inline ECVarFlags operator|(ECVarFlags a, ECVarFlags b)
    {
        return static_cast<ECVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ECVarFlags& operator|=(ECVarFlags& a, ECVarFlags b)
    {
        a = a | b;
        return a;
    }

    inline bool HasFlag(ECVarFlags value, ECVarFlags flag)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    struct FConsoleResult
    {
        bool success;
        std::string message;
        std::vector<std::string> output;

        static FConsoleResult Success(const std::string& msg = "")
        {
            return {true, msg, {}};
        }

        static FConsoleResult Failure(const std::string& msg)
        {
            return {false, msg, {}};
        }
    };

    struct FCVarMatchOptions
    {
        bool prefixThenSubstring = false;
        bool includeValue = false;
        size_t limit = 0;
    };

    struct FCVarInfo
    {
        std::string name;
        std::string description;
        ECVarType type = ECVarType::Int;
        ECVarFlags flags = ECVarFlags::None;
        bool isDefault = true;
        bool isUnsigned = false;
        std::optional<double> minValue;
        std::optional<double> maxValue;
    };

    class FCVarSystem final
    {
    public:
        using FCVarValue = std::variant<int64_t, double, bool, std::string>;

        bool RegisterInt(const std::string& name, int32_t defaultValue, int32_t* target,
                         ECVarFlags flags, std::string description,
                         std::function<void()> onChanged = nullptr,
                         std::optional<int64_t> minValue = std::nullopt,
                         std::optional<int64_t> maxValue = std::nullopt);
        bool RegisterUInt(const std::string& name, uint32_t defaultValue, uint32_t* target,
                          ECVarFlags flags, std::string description,
                          std::function<void()> onChanged = nullptr,
                          std::optional<uint64_t> minValue = std::nullopt,
                          std::optional<uint64_t> maxValue = std::nullopt);
        bool RegisterFloat(const std::string& name, float defaultValue, float* target,
                           ECVarFlags flags, std::string description,
                           std::function<void()> onChanged = nullptr,
                           std::optional<double> minValue = std::nullopt,
                           std::optional<double> maxValue = std::nullopt);
        bool RegisterBool(const std::string& name, bool defaultValue, bool* target,
                          ECVarFlags flags, std::string description,
                          std::function<void()> onChanged = nullptr);
        bool RegisterString(const std::string& name, std::string defaultValue, std::string* target,
                            ECVarFlags flags, std::string description,
                            std::function<void()> onChanged = nullptr);

        bool LoadDefaultFile(const std::string& path);
        bool LoadUserFile(const std::string& path);
        bool SaveUserFile(const std::string& path) const;
        void RegisterUserFileChannel(std::string prefix, std::string path);
        bool LoadUserFiles();
        bool SaveUserFiles() const;

        FConsoleResult ExecuteCommand(const std::string& line);

        bool SetValueFromString(const std::string& name, const std::string& value,
                                ECVarSetBy setBy, std::string* outError);
        bool SetDefaultFromString(const std::string& name, const std::string& value, std::string* outError);
        std::string GetValueString(const std::string& name, bool* found = nullptr) const;
        bool ResetToDefault(const std::string& name);
        bool TryGetInfo(const std::string& name, FCVarInfo& outInfo) const;
        void ForEach(const std::function<void(const FCVarInfo&)>& fn) const;
        std::vector<std::string> Match(const std::string& query, const FCVarMatchOptions& options,
                                       size_t* totalMatches = nullptr) const;

    private:
        // Shared implementation behind the five typed Register* entry points.
        // StoredT is the variant alternative the value is stored as.
        template <typename StoredT, typename T>
        bool RegisterTyped(const std::string& name, T defaultValue, T* target,
                           ECVarFlags flags, std::string description,
                           std::function<void()> onChanged,
                           std::optional<double> minValue = std::nullopt,
                           std::optional<double> maxValue = std::nullopt,
                           bool isUnsigned = false);

        struct FCVarEntry
        {
            std::string name;
            std::string description;
            ECVarType type = ECVarType::Int;
            ECVarFlags flags = ECVarFlags::None;
            ECVarSetBy setBy = ECVarSetBy::Default;
            FCVarValue defaultValue;
            FCVarValue value;
            std::optional<double> minValue;
            std::optional<double> maxValue;
            bool isUnsigned = false;
            std::variant<std::monostate, int32_t*, uint32_t*, float*, bool*, std::string*> boundTarget;
            std::function<void()> onChanged;
        };

        std::unordered_map<std::string, FCVarEntry> cvars_;
        std::string defaultConfigPath_;
        std::string userConfigPath_ = "assets/configs/cvar_user.json";
        std::vector<std::pair<std::string, std::string>> userFileChannels_;

        static std::string Trim(const std::string& input);
        static std::vector<std::string> Split(const std::string& input);
        static std::string ToLower(std::string value);
        static std::string Unquote(const std::string& value);

        bool ApplyDefaultValue(FCVarEntry& entry, const FCVarValue& value);
        bool SetEntryValue(FCVarEntry& entry, const FCVarValue& value, ECVarSetBy setBy, std::string* outError);
        FCVarValue GetEntryValue(const FCVarEntry& entry) const;
        FCVarInfo MakeInfo(const FCVarEntry& entry) const;
        bool IsDefaultValue(const FCVarEntry& entry) const;
        bool SaveEntries(const std::string& path,
                         const std::function<bool(const std::string&)>& includeName) const;
        std::string ChannelPathForName(const std::string& name) const;
        std::string ToString(const FCVarValue& value, ECVarType type) const;
        std::string FlagsToString(ECVarFlags flags) const;
        bool ParseValue(const std::string& valueText, ECVarType type, FCVarValue& outValue, std::string* outError) const;
    };
}
