#pragma once

// Loopback transport used only by the agent-validation module.

#include "Engine/Common/CoreMinimal.hpp"

#include <nlohmann/json.hpp>

namespace Runtime::Agent
{
    class FAgentControlServer
    {
    public:
        using Handler = std::function<nlohmann::json(const std::string&, const nlohmann::json&)>;
        FAgentControlServer();
        ~FAgentControlServer();
        bool Start(const std::string& endpoint, std::string token, std::string& error);
        void Stop();
        void Pump(const Handler& handler);
        bool IsRunning() const;

    private:
        struct FImpl;
        std::unique_ptr<FImpl> impl_;
    };
}
