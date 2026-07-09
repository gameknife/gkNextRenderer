#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <optional>
#include <variant>

class NextEngine;

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

    class IAgentDriver
    {
    public:
        virtual ~IAgentDriver() = default;

        virtual bool IsLoaded() const = 0;
        virtual void Tick(double deltaSeconds) = 0;
        virtual void DrawStatusOverlay() const = 0;
        virtual int ExitCode() const = 0;
    };

    using AgentDriverFactory = std::function<std::unique_ptr<IAgentDriver>(NextEngine&)>;
}
