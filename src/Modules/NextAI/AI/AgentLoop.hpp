#pragma once

#include "Modules/NextAI/AI/AIChat.hpp"
#include "Modules/NextAI/AI/IAITool.hpp"
#include "Modules/NextAI/AI/ToolRegistry.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace NextAI
{
    class FMainThreadDispatcher;

    using FChatProviderFn = std::function<FChatResponse(const FChatRequest&)>;

    struct FAgentOptions
    {
        int maxSteps = 12;
        std::chrono::milliseconds mainThreadTimeout{5000};

        // When the LLM emits a final answer without ever invoking a tool, retry up
        // to this many times with a reminder appended. 0 disables grounding.
        int maxGroundingRetries = 2;
        std::string groundingReminder =
            "You finished without calling any tool. If the user's request requires "
            "looking up information or modifying state, you MUST call the appropriate "
            "tools first. Try again.";

        // Model name to override per-request; empty leaves provider default.
        std::string model;
        float temperature = 0.7f;
        int maxTokens = 0;
    };

    struct FAgentResult
    {
        bool success = false;
        std::string finalContent;
        std::string errorMessage;
        int stepsUsed = 0;
        int toolCallsExecuted = 0;
        int groundingRetries = 0;
        bool cancelled = false;
        bool maxStepsExceeded = false;
        std::vector<FChatMessage> transcript;
    };

    class FAgentLoop
    {
    public:
        static FAgentResult Run(
            FChatRequest seed,
            const FToolRegistry& tools,
            FChatProviderFn provider,
            FAgentOptions options = {},
            FAgentEventSink* sink = nullptr,
            FMainThreadDispatcher* mainThread = nullptr,
            std::atomic<bool>* cancelFlag = nullptr);

        // Best-effort fallback parser for models that emit tool calls as raw JSON
        // inside ```json fences or as a top-level object instead of using the OpenAI
        // tool_calls schema. Returns empty vector if nothing matched.
        static std::vector<FToolCall> ParseFallbackToolCalls(const std::string& content);
    };
}
