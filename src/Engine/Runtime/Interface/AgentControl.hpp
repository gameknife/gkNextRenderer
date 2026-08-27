#pragma once

namespace Runtime::Agent
{
    class IAgentControlService
    {
    public:
        virtual ~IAgentControlService() = default;
        virtual bool IsRunning() const = 0;
        virtual void Pump() = 0;
        virtual void Stop() = 0;
    };
}
