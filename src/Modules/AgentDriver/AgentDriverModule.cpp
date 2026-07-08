#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/AgentDriver/AgentDriverModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/AgentDriver/AgentDriver.hpp"

namespace Modules::AgentDriver
{
    void Install(NextEngine& engine)
    {
        engine.SetAgentDriverFactory([](NextEngine& owner) -> std::unique_ptr<Runtime::Agent::IAgentDriver>
        {
            return std::make_unique<Runtime::Agent::FAgentDriver>(owner);
        });
    }
}
