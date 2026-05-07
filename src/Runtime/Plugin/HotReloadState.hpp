#pragma once

#include "Common/CoreMinimal.hpp"

#include <nlohmann/json.hpp>

class FHotReloadState final
{
public:
    template <typename T>
    void Set(std::string_view key, const T& value)
    {
        document_[std::string(key)] = value;
    }

    template <typename T>
    bool Get(std::string_view key, T& outValue) const
    {
        const auto it = document_.find(std::string(key));
        if (it == document_.end())
        {
            return false;
        }

        try
        {
            outValue = it->get<T>();
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    nlohmann::json& Raw() { return document_; }
    const nlohmann::json& Raw() const { return document_; }
    bool Empty() const { return document_.empty(); }

private:
    nlohmann::json document_ = nlohmann::json::object();
};
