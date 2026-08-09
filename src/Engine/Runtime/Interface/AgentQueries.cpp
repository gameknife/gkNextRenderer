#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"

namespace Runtime::Agent
{
    void FAgentQueryRegistry::Add(std::string name, QueryFn fn) { if (!name.empty() && fn) queries_[std::move(name)] = std::move(fn); }
    std::optional<FAgentQueryValue> FAgentQueryRegistry::Query(const std::string& name) const { const auto it = queries_.find(name); return it == queries_.end() ? std::nullopt : std::optional<FAgentQueryValue>(it->second()); }
    std::vector<std::string> FAgentQueryRegistry::Names() const { std::vector<std::string> result; for (const auto& [name, fn] : queries_) result.push_back(name); std::sort(result.begin(), result.end()); return result; }
    std::string ToString(const FAgentQueryValue& value)
    {
        if (const auto* item = std::get_if<bool>(&value)) return *item ? "true" : "false";
        if (const auto* item = std::get_if<int64_t>(&value)) return std::to_string(*item);
        if (const auto* item = std::get_if<double>(&value)) return fmt::format("{}", *item);
        return std::get<std::string>(value);
    }
}
