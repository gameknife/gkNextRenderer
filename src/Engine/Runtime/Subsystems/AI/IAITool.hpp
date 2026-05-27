#pragma once

#include "Engine/Runtime/Subsystems/AI/AIChat.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace NextAI
{
    class FMainThreadDispatcher;

    struct FToolCallContext
    {
        FMainThreadDispatcher* mainThread = nullptr;
        std::atomic<bool>* cancelFlag = nullptr;
        int step = 0;
    };

    class IAITool
    {
    public:
        virtual ~IAITool() = default;

        virtual std::string Name() const = 0;
        virtual FToolSchema Schema() const = 0;

        // Tools that mutate engine state (scene/QuickJS/Vulkan) must return true so
        // FAgentLoop marshals Execute through FMainThreadDispatcher.
        virtual bool RequiresMainThread() const { return false; }

        // High-risk tools should not execute eagerly; FEditorAIService translates
        // these into a FDeferredEditorAction the user must confirm.
        virtual bool IsHighRisk() const { return false; }

        // Returns the tool result text appended back to the LLM as a tool-role message.
        // Throw or prefix the result with "ERROR:" to signal failure.
        virtual std::string Execute(const nlohmann::json& args, FToolCallContext& ctx) = 0;
    };

    struct FAgentToolEvent
    {
        enum class EPhase
        {
            Call,    // LLM requested a tool call
            Result,  // tool produced a result
            Error,   // tool error (lookup / args / exception)
            Final,   // LLM produced final assistant content
            Cancelled
        };

        int step = 0;
        EPhase phase = EPhase::Call;
        std::string toolName;
        std::string summary;
        std::string detail;
    };

    class FAgentEventSink
    {
    public:
        virtual ~FAgentEventSink() = default;
        virtual void Emit(const FAgentToolEvent& evt) = 0;
    };

    class FNullAgentEventSink : public FAgentEventSink
    {
    public:
        void Emit(const FAgentToolEvent&) override {}
    };

    // Thread-safe sink for UI consumption.
    class FInMemoryAgentEventSink : public FAgentEventSink
    {
    public:
        void Emit(const FAgentToolEvent& evt) override;
        std::vector<FAgentToolEvent> Drain();
        size_t Size() const;

    private:
        mutable std::mutex mutex_;
        std::vector<FAgentToolEvent> events_;
    };

    const char* AgentEventPhaseToString(FAgentToolEvent::EPhase phase);
}
