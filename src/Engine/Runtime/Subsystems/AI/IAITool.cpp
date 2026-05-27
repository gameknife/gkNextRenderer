#include "Engine/Runtime/Subsystems/AI/IAITool.hpp"

namespace NextAI
{
    void FInMemoryAgentEventSink::Emit(const FAgentToolEvent& evt)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(evt);
    }

    std::vector<FAgentToolEvent> FInMemoryAgentEventSink::Drain()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FAgentToolEvent> out = std::move(events_);
        events_.clear();
        return out;
    }

    size_t FInMemoryAgentEventSink::Size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    const char* AgentEventPhaseToString(FAgentToolEvent::EPhase phase)
    {
        switch (phase)
        {
        case FAgentToolEvent::EPhase::Call: return "call";
        case FAgentToolEvent::EPhase::Result: return "result";
        case FAgentToolEvent::EPhase::Error: return "error";
        case FAgentToolEvent::EPhase::Final: return "final";
        case FAgentToolEvent::EPhase::Cancelled: return "cancelled";
        }
        return "unknown";
    }
}
