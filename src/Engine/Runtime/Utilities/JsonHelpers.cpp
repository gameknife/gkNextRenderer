#include "Engine/Runtime/Utilities/JsonHelpers.h"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <fstream>
#include <spdlog/spdlog.h>

namespace
{
    [[noreturn]] void LogAndThrow(std::string_view message)
    {
        SPDLOG_ERROR("[NextJson] {}", message);
        Throw(std::runtime_error(std::string(message)));
    }
}

namespace NextJson
{
    nlohmann::json LoadFile(const std::string& path)
    {
        const std::string absolutePath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
        std::ifstream input(absolutePath);
        if (!input.is_open())
        {
            LogAndThrow(fmt::format("Failed to open JSON file: {}", absolutePath));
        }

        try
        {
            nlohmann::json document;
            input >> document;
            return document;
        }
        catch (const std::exception& exception)
        {
            LogAndThrow(fmt::format("Failed to parse JSON file {}: {}", absolutePath, exception.what()));
        }
    }

    bool TryLoadFile(const std::string& path, nlohmann::json& outDocument)
    {
        try
        {
            outDocument = LoadFile(path);
            return true;
        }
        catch (const std::exception& exception)
        {
            SPDLOG_ERROR("[NextJson] {}", exception.what());
            outDocument = {};
            return false;
        }
    }

    glm::vec3 GetVec3(const nlohmann::json& object, const char* key, const glm::vec3& fallback)
    {
        if (!object.is_object() || !object.contains(key))
        {
            return fallback;
        }

        const nlohmann::json& value = object.at(key);
        if (!value.is_array() || value.size() != 3)
        {
            return fallback;
        }

        try
        {
            return glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    glm::vec3 GetRequiredVec3(const nlohmann::json& object, const char* key, std::string_view context)
    {
        if (!object.is_object())
        {
            throw std::runtime_error(fmt::format("{} is not a JSON object", context));
        }

        if (!object.contains(key))
        {
            throw std::runtime_error(fmt::format("{} is missing required field '{}'", context, key));
        }

        const nlohmann::json& value = object.at(key);
        if (!value.is_array() || value.size() != 3)
        {
            throw std::runtime_error(fmt::format("{} field '{}' must be an array of 3 floats", context, key));
        }

        try
        {
            return glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(fmt::format("{} field '{}' has invalid value: {}", context, key, exception.what()));
        }
    }

    bool HasObject(const nlohmann::json& document, const char* key)
    {
        return document.contains(key) && document.at(key).is_object();
    }

    bool HasArray(const nlohmann::json& document, const char* key)
    {
        return document.contains(key) && document.at(key).is_array();
    }

    void RequireObject(const nlohmann::json& document, const char* key, std::string_view context)
    {
        if (!HasObject(document, key))
        {
            throw std::runtime_error(fmt::format("{} is missing required object '{}'", context, key));
        }
    }

    void RequireArray(const nlohmann::json& document, const char* key, std::string_view context)
    {
        if (!HasArray(document, key))
        {
            throw std::runtime_error(fmt::format("{} is missing required array '{}'", context, key));
        }
    }
}
