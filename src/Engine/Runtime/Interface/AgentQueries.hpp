#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <optional>
#include <variant>

namespace Runtime::Agent
{
    using FAgentQueryValue = std::variant<bool, int64_t, double, std::string>;
    class FAgentQueryRegistry final
    {
    public:
        using QueryFn = std::function<FAgentQueryValue()>;
        void Add(std::string name, QueryFn fn);
        std::optional<FAgentQueryValue> Query(const std::string& name) const;
        std::vector<std::string> Names() const;
    private:
        std::unordered_map<std::string, QueryFn> queries_;
    };
    std::string ToString(const FAgentQueryValue& value);
}
