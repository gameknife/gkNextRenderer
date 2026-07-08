#include "Engine/Runtime/Interface/AgentDriver.hpp"

#include <algorithm>
#include <fmt/format.h>

namespace Runtime::Agent
{
    void FAgentQueryRegistry::Add(std::string name, QueryFn fn)
    {
        if (!name.empty() && fn)
        {
            queries_[std::move(name)] = std::move(fn);
        }
    }

    std::optional<FAgentQueryValue> FAgentQueryRegistry::Query(const std::string& name) const
    {
        const auto it = queries_.find(name);
        if (it == queries_.end())
        {
            return std::nullopt;
        }
        return it->second();
    }

    std::vector<std::string> FAgentQueryRegistry::Names() const
    {
        std::vector<std::string> names;
        names.reserve(queries_.size());
        for (const auto& [name, _] : queries_)
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string ToString(const FAgentQueryValue& value)
    {
        if (std::holds_alternative<bool>(value))
        {
            return std::get<bool>(value) ? "true" : "false";
        }
        if (std::holds_alternative<int64_t>(value))
        {
            return fmt::format("{}", std::get<int64_t>(value));
        }
        if (std::holds_alternative<double>(value))
        {
            return fmt::format("{}", std::get<double>(value));
        }
        return std::get<std::string>(value);
    }
}
