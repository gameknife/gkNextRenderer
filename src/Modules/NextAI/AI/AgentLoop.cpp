#include "Modules/NextAI/AI/AgentLoop.hpp"
#include "Modules/NextAI/AI/MainThreadDispatcher.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <regex>

using json = nlohmann::json;

namespace NextAI
{
    namespace
    {
        FNullAgentEventSink g_nullSink;

        void Emit(FAgentEventSink* sink, FAgentToolEvent evt)
        {
            (sink ? sink : &g_nullSink)->Emit(std::move(evt));
        }

        std::string Shorten(const std::string& s, size_t limit = 120)
        {
            if (s.size() <= limit) return s;
            return s.substr(0, limit) + "...";
        }

        // Look for the first ```json ... ``` fence and parse a tool call out of it.
        std::vector<FToolCall> TryParseJsonFence(const std::string& content)
        {
            std::vector<FToolCall> out;
            std::regex fence(R"(```\s*json\s*\n([\s\S]*?)```)");
            std::smatch match;
            if (!std::regex_search(content, match, fence)) return out;
            const std::string payload = match[1].str();
            try
            {
                json parsed = json::parse(payload);
                auto extract = [&out](const json& obj)
                {
                    if (!obj.is_object()) return;
                    std::string name;
                    if (obj.contains("name") && obj["name"].is_string())
                    {
                        name = obj["name"].get<std::string>();
                    }
                    else if (obj.contains("tool") && obj["tool"].is_string())
                    {
                        name = obj["tool"].get<std::string>();
                    }
                    if (name.empty()) return;
                    FToolCall call;
                    call.name = name;
                    if (obj.contains("arguments"))
                    {
                        call.argumentsJson = obj["arguments"].is_string()
                            ? obj["arguments"].get<std::string>()
                            : obj["arguments"].dump();
                    }
                    else if (obj.contains("args"))
                    {
                        call.argumentsJson = obj["args"].is_string()
                            ? obj["args"].get<std::string>()
                            : obj["args"].dump();
                    }
                    else
                    {
                        call.argumentsJson = "{}";
                    }
                    call.id = "fallback_" + std::to_string(out.size());
                    out.push_back(std::move(call));
                };
                if (parsed.is_array())
                {
                    for (const auto& item : parsed) extract(item);
                }
                else
                {
                    extract(parsed);
                }
            }
            catch (...)
            {
                // ignore malformed fence
            }
            return out;
        }

        bool CheckCancel(std::atomic<bool>* flag)
        {
            return flag && flag->load(std::memory_order_relaxed);
        }
    }

    std::vector<FToolCall> FAgentLoop::ParseFallbackToolCalls(const std::string& content)
    {
        if (auto fromFence = TryParseJsonFence(content); !fromFence.empty())
        {
            return fromFence;
        }
        // Last resort: the whole content is a bare JSON object/array.
        try
        {
            json parsed = json::parse(content);
            std::vector<FToolCall> out;
            auto extract = [&out](const json& obj)
            {
                if (!obj.is_object()) return;
                if (!obj.contains("name") && !obj.contains("tool")) return;
                FToolCall call;
                call.name = obj.contains("name") ? obj["name"].get<std::string>()
                                                 : obj["tool"].get<std::string>();
                if (obj.contains("arguments"))
                {
                    call.argumentsJson = obj["arguments"].is_string()
                        ? obj["arguments"].get<std::string>()
                        : obj["arguments"].dump();
                }
                else if (obj.contains("args"))
                {
                    call.argumentsJson = obj["args"].is_string()
                        ? obj["args"].get<std::string>()
                        : obj["args"].dump();
                }
                else
                {
                    call.argumentsJson = "{}";
                }
                call.id = "fallback_" + std::to_string(out.size());
                out.push_back(std::move(call));
            };
            if (parsed.is_array())
            {
                for (const auto& item : parsed) extract(item);
            }
            else
            {
                extract(parsed);
            }
            return out;
        }
        catch (...)
        {
            return {};
        }
    }

    FAgentResult FAgentLoop::Run(
        FChatRequest seed,
        const FToolRegistry& tools,
        FChatProviderFn provider,
        FAgentOptions options,
        FAgentEventSink* sink,
        FMainThreadDispatcher* mainThread,
        std::atomic<bool>* cancelFlag)
    {
        FAgentResult result;

        if (!provider)
        {
            result.errorMessage = "no provider supplied";
            return result;
        }
        if (options.maxSteps <= 0)
        {
            result.errorMessage = "maxSteps must be positive";
            return result;
        }

        // Bake tool schemas + model/temperature defaults into the request the
        // provider will see; do NOT clobber caller-supplied seed.tools.
        if (seed.tools.empty() && !tools.Empty())
        {
            seed.tools = tools.BuildSchemas();
        }
        if (seed.model.empty()) seed.model = options.model;
        if (seed.temperature <= 0.0f) seed.temperature = options.temperature;
        if (seed.maxTokens <= 0) seed.maxTokens = options.maxTokens;

        std::vector<FChatMessage> messages = seed.messages;
        bool anyToolCallRequested = false;

        for (int step = 1; step <= options.maxSteps; ++step)
        {
            result.stepsUsed = step;

            if (CheckCancel(cancelFlag))
            {
                result.cancelled = true;
                result.errorMessage = "cancelled by caller";
                Emit(sink, {step, FAgentToolEvent::EPhase::Cancelled, "", "cancelled", ""});
                result.transcript = std::move(messages);
                return result;
            }

            FChatRequest req = seed;
            req.messages = messages;

            FChatResponse resp = provider(req);
            if (!resp.success)
            {
                result.errorMessage = resp.errorMessage;
                Emit(sink, {step, FAgentToolEvent::EPhase::Error, "", "provider error", resp.errorMessage});
                result.transcript = std::move(messages);
                return result;
            }

            std::vector<FToolCall> calls = resp.toolCalls;
            if (calls.empty() && !resp.content.empty())
            {
                calls = ParseFallbackToolCalls(resp.content);
            }

            if (calls.empty())
            {
                // Final answer candidate. Apply grounding retry only if the LLM has
                // never even attempted a tool call (a failed call still counts —
                // the model knew tools existed, the error itself will guide it).
                if (!anyToolCallRequested && result.groundingRetries < options.maxGroundingRetries
                    && !options.groundingReminder.empty() && !tools.Empty())
                {
                    ++result.groundingRetries;
                    FChatMessage assistantTurn = FChatMessage::Assistant(resp.content);
                    messages.push_back(std::move(assistantTurn));
                    messages.push_back(FChatMessage::User(options.groundingReminder));
                    Emit(sink, {step, FAgentToolEvent::EPhase::Error, "",
                                "premature final; injecting grounding reminder",
                                Shorten(resp.content)});
                    continue;
                }

                result.success = true;
                result.finalContent = resp.content;
                messages.push_back(FChatMessage::Assistant(resp.content));
                Emit(sink, {step, FAgentToolEvent::EPhase::Final, "", "final answer", Shorten(resp.content)});
                result.transcript = std::move(messages);
                return result;
            }

            // Append assistant message with tool_calls (content may be empty).
            anyToolCallRequested = true;
            FChatMessage assistantTurn;
            assistantTurn.role = EChatRole::Assistant;
            assistantTurn.content = resp.content;
            assistantTurn.toolCalls = calls;
            messages.push_back(assistantTurn);

            for (auto& call : calls)
            {
                Emit(sink, {step, FAgentToolEvent::EPhase::Call, call.name,
                            "calling " + call.name, Shorten(call.argumentsJson)});

                IAITool* tool = tools.Find(call.name);
                if (!tool)
                {
                    const std::string err = "ERROR: unknown tool '" + call.name + "'";
                    messages.push_back(FChatMessage::ToolResult(call.id, call.name, err));
                    Emit(sink, {step, FAgentToolEvent::EPhase::Error, call.name, "unknown tool", err});
                    continue;
                }

                json args = json::object();
                if (!call.argumentsJson.empty())
                {
                    try
                    {
                        args = json::parse(call.argumentsJson);
                    }
                    catch (const std::exception& e)
                    {
                        const std::string err = std::string("ERROR: invalid arguments JSON: ") + e.what();
                        messages.push_back(FChatMessage::ToolResult(call.id, call.name, err));
                        Emit(sink, {step, FAgentToolEvent::EPhase::Error, call.name, "bad arguments", err});
                        continue;
                    }
                }

                FToolCallContext ctx;
                ctx.mainThread = mainThread;
                ctx.cancelFlag = cancelFlag;
                ctx.step = step;

                std::string toolResult;
                try
                {
                    if (tool->RequiresMainThread() && mainThread != nullptr)
                    {
                        // Marshal execution onto the main thread.
                        IAITool* toolPtr = tool;
                        toolResult = mainThread->SubmitAndWait(
                            [toolPtr, &args, &ctx]() { return toolPtr->Execute(args, ctx); },
                            options.mainThreadTimeout);
                    }
                    else
                    {
                        toolResult = tool->Execute(args, ctx);
                    }
                }
                catch (const std::exception& e)
                {
                    toolResult = std::string("ERROR: tool threw exception: ") + e.what();
                }
                catch (...)
                {
                    toolResult = "ERROR: tool threw unknown exception";
                }

                ++result.toolCallsExecuted;
                messages.push_back(FChatMessage::ToolResult(call.id, call.name, toolResult));
                Emit(sink, {step, FAgentToolEvent::EPhase::Result, call.name,
                            "tool result", Shorten(toolResult)});
            }
        }

        result.maxStepsExceeded = true;
        result.success = false;
        result.errorMessage = "exceeded max steps without producing final answer";
        result.transcript = std::move(messages);
        Emit(sink, {options.maxSteps, FAgentToolEvent::EPhase::Error, "",
                    "max steps exceeded", result.errorMessage});
        return result;
    }
}
