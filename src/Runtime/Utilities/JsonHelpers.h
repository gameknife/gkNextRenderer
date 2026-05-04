#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <string_view>

namespace NextJson
{
    nlohmann::json LoadFile(const std::string& path);
    bool TryLoadFile(const std::string& path, nlohmann::json& outDocument);

    template <typename TValue>
    TValue GetRequired(const nlohmann::json& object, const char* key, std::string_view context)
    {
        if (!object.is_object())
        {
            throw std::runtime_error(fmt::format("{} is not a JSON object", context));
        }

        if (!object.contains(key))
        {
            throw std::runtime_error(fmt::format("{} is missing required field '{}'", context, key));
        }

        try
        {
            return object.at(key).get<TValue>();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(fmt::format("{} field '{}' has invalid type: {}", context, key, exception.what()));
        }
    }

    template <typename TValue>
    TValue GetOptional(const nlohmann::json& object, const char* key, TValue defaultValue)
    {
        if (!object.is_object() || !object.contains(key))
        {
            return defaultValue;
        }

        try
        {
            return object.at(key).get<TValue>();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(fmt::format("field '{}' has invalid type: {}", key, exception.what()));
        }
    }

    template <typename TValue>
    TValue GetOptional(const nlohmann::json& object, const char* key, std::string_view context, TValue defaultValue)
    {
        if (!object.is_object() || !object.contains(key))
        {
            return defaultValue;
        }

        try
        {
            return object.at(key).get<TValue>();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(fmt::format("{} field '{}' has invalid type: {}", context, key, exception.what()));
        }
    }

    glm::vec3 GetVec3(const nlohmann::json& object, const char* key, const glm::vec3& fallback);
    glm::vec3 GetRequiredVec3(const nlohmann::json& object, const char* key, std::string_view context);

    bool HasObject(const nlohmann::json& document, const char* key);
    bool HasArray(const nlohmann::json& document, const char* key);
    void RequireObject(const nlohmann::json& document, const char* key, std::string_view context);
    void RequireArray(const nlohmann::json& document, const char* key, std::string_view context);
}
