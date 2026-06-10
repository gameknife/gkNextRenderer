#include "Modules/NextAI/AI/AIChat.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace NextAI
{
    const char* ChatRoleToString(EChatRole r)
    {
        switch (r)
        {
        case EChatRole::System: return "system";
        case EChatRole::User: return "user";
        case EChatRole::Assistant: return "assistant";
        case EChatRole::Tool: return "tool";
        }
        return "user";
    }

    const char* ToolParamTypeToString(EToolParamType t)
    {
        switch (t)
        {
        case EToolParamType::String: return "string";
        case EToolParamType::Number: return "number";
        case EToolParamType::Integer: return "integer";
        case EToolParamType::Boolean: return "boolean";
        case EToolParamType::Object: return "object";
        case EToolParamType::Array: return "array";
        }
        return "string";
    }

    static json BuildToolSchemaOpenAI(const FToolSchema& tool)
    {
        json properties = json::object();
        json required = json::array();
        for (const auto& p : tool.params)
        {
            properties[p.name] = {
                {"type", ToolParamTypeToString(p.type)},
                {"description", p.description},
            };
            if (p.required)
            {
                required.push_back(p.name);
            }
        }
        return {
            {"type", "function"},
            {"function", {
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", {
                    {"type", "object"},
                    {"properties", properties},
                    {"required", required},
                }},
            }},
        };
    }

    json BuildOpenAIChatRequestBody(const FChatRequest& req, bool injectThinkingControl)
    {
        json messages = json::array();
        for (const auto& m : req.messages)
        {
            json entry = {{"role", ChatRoleToString(m.role)}};
            // OpenAI requires content for system/user/assistant; tool messages may omit it
            // but require tool_call_id. Use empty string default when absent.
            entry["content"] = m.content;
            if (m.role == EChatRole::Tool)
            {
                if (!m.toolCallId.empty())
                {
                    entry["tool_call_id"] = m.toolCallId;
                }
                if (!m.name.empty())
                {
                    entry["name"] = m.name;
                }
            }
            if (m.role == EChatRole::Assistant && !m.toolCalls.empty())
            {
                json calls = json::array();
                for (const auto& tc : m.toolCalls)
                {
                    calls.push_back({
                        {"id", tc.id},
                        {"type", "function"},
                        {"function", {
                            {"name", tc.name},
                            {"arguments", tc.argumentsJson.empty() ? std::string("{}") : tc.argumentsJson},
                        }},
                    });
                }
                entry["tool_calls"] = calls;
            }
            messages.push_back(entry);
        }

        json body = {
            {"messages", messages},
            {"stream", false},
        };
        if (!req.model.empty())
        {
            body["model"] = req.model;
        }
        if (req.temperature > 0.0f)
        {
            body["temperature"] = req.temperature;
        }
        if (req.maxTokens > 0)
        {
            body["max_tokens"] = req.maxTokens;
        }
        if (!req.tools.empty())
        {
            json tools = json::array();
            for (const auto& t : req.tools)
            {
                tools.push_back(BuildToolSchemaOpenAI(t));
            }
            body["tools"] = tools;
        }
        if (injectThinkingControl)
        {
            // llama-server / Qwen3-style chat templates gate the <think> block via
            // chat_template_kwargs.enable_thinking. Disabling it keeps reasoning out
            // of content and avoids confusing the tool-call parser.
            body["chat_template_kwargs"] = {{"enable_thinking", req.enableThinking}};
        }
        return body;
    }

    FChatResponse ParseOpenAIChatResponse(const json& body)
    {
        if (body.contains("error"))
        {
            std::string msg;
            const auto& err = body["error"];
            if (err.is_object() && err.contains("message"))
            {
                msg = err["message"].get<std::string>();
            }
            else
            {
                msg = err.dump();
            }
            return FChatResponse::Failure(msg);
        }
        if (!body.contains("choices") || body["choices"].empty())
        {
            return FChatResponse::Failure("no choices in response");
        }
        const auto& choice = body["choices"][0];
        FChatResponse resp;
        resp.success = true;
        if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
        {
            resp.finishReason = choice["finish_reason"].get<std::string>();
        }
        if (choice.contains("message"))
        {
            const auto& msg = choice["message"];
            if (msg.contains("content") && msg["content"].is_string())
            {
                resp.content = msg["content"].get<std::string>();
            }
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array())
            {
                for (const auto& tc : msg["tool_calls"])
                {
                    FToolCall call;
                    if (tc.contains("id") && tc["id"].is_string())
                    {
                        call.id = tc["id"].get<std::string>();
                    }
                    if (tc.contains("function") && tc["function"].is_object())
                    {
                        const auto& fn = tc["function"];
                        if (fn.contains("name") && fn["name"].is_string())
                        {
                            call.name = fn["name"].get<std::string>();
                        }
                        if (fn.contains("arguments"))
                        {
                            // Arguments may be a JSON-encoded string or a raw object.
                            if (fn["arguments"].is_string())
                            {
                                call.argumentsJson = fn["arguments"].get<std::string>();
                            }
                            else
                            {
                                call.argumentsJson = fn["arguments"].dump();
                            }
                        }
                    }
                    resp.toolCalls.push_back(std::move(call));
                }
            }
        }
        if (body.contains("usage") && body["usage"].is_object())
        {
            const auto& u = body["usage"];
            if (u.contains("prompt_tokens") && u["prompt_tokens"].is_number_integer())
            {
                resp.usage.promptTokens = u["prompt_tokens"].get<int>();
            }
            if (u.contains("completion_tokens") && u["completion_tokens"].is_number_integer())
            {
                resp.usage.completionTokens = u["completion_tokens"].get<int>();
            }
        }
        return resp;
    }

    static json BuildGeminiFunctionDeclaration(const FToolSchema& tool)
    {
        json properties = json::object();
        json required = json::array();
        for (const auto& p : tool.params)
        {
            properties[p.name] = {
                {"type", ToolParamTypeToString(p.type)},
                {"description", p.description},
            };
            if (p.required)
            {
                required.push_back(p.name);
            }
        }
        return {
            {"name", tool.name},
            {"description", tool.description},
            {"parameters", {
                {"type", "object"},
                {"properties", properties},
                {"required", required},
            }},
        };
    }

    json BuildGeminiChatRequestBody(const FChatRequest& req)
    {
        json contents = json::array();
        std::string systemText;
        for (const auto& m : req.messages)
        {
            if (m.role == EChatRole::System)
            {
                if (!systemText.empty()) systemText += "\n";
                systemText += m.content;
                continue;
            }
            const char* roleStr = (m.role == EChatRole::Assistant) ? "model" : "user";
            json parts = json::array();
            if (m.role == EChatRole::Tool)
            {
                // Gemini represents tool results as functionResponse parts.
                json fnResp = {
                    {"name", m.name},
                    {"response", {{"content", m.content}}},
                };
                parts.push_back({{"functionResponse", fnResp}});
            }
            else if (m.role == EChatRole::Assistant && !m.toolCalls.empty())
            {
                for (const auto& tc : m.toolCalls)
                {
                    json args;
                    try
                    {
                        args = tc.argumentsJson.empty() ? json::object() : json::parse(tc.argumentsJson);
                    }
                    catch (...)
                    {
                        args = json::object();
                    }
                    parts.push_back({
                        {"functionCall", {{"name", tc.name}, {"args", args}}}
                    });
                }
                if (!m.content.empty())
                {
                    parts.push_back({{"text", m.content}});
                }
            }
            else
            {
                parts.push_back({{"text", m.content}});
            }
            contents.push_back({{"role", roleStr}, {"parts", parts}});
        }

        json body = {{"contents", contents}};
        json gen = json::object();
        if (req.temperature > 0.0f) gen["temperature"] = req.temperature;
        if (req.maxTokens > 0) gen["maxOutputTokens"] = req.maxTokens;
        if (!gen.empty()) body["generationConfig"] = gen;
        if (!systemText.empty())
        {
            body["systemInstruction"] = {{"parts", json::array({{{"text", systemText}}})}};
        }
        if (!req.tools.empty())
        {
            json decls = json::array();
            for (const auto& t : req.tools)
            {
                decls.push_back(BuildGeminiFunctionDeclaration(t));
            }
            body["tools"] = json::array({{{"functionDeclarations", decls}}});
        }
        return body;
    }

    FChatResponse ParseGeminiChatResponse(const json& body)
    {
        if (body.contains("error"))
        {
            std::string msg;
            const auto& err = body["error"];
            if (err.is_object() && err.contains("message"))
            {
                msg = err["message"].get<std::string>();
            }
            else
            {
                msg = err.dump();
            }
            return FChatResponse::Failure(msg);
        }
        if (!body.contains("candidates") || body["candidates"].empty())
        {
            return FChatResponse::Failure("no candidates in response");
        }
        const auto& candidate = body["candidates"][0];
        FChatResponse resp;
        resp.success = true;
        if (candidate.contains("finishReason") && candidate["finishReason"].is_string())
        {
            resp.finishReason = candidate["finishReason"].get<std::string>();
        }
        if (candidate.contains("content") && candidate["content"].contains("parts"))
        {
            const auto& parts = candidate["content"]["parts"];
            for (const auto& part : parts)
            {
                if (part.contains("text") && part["text"].is_string())
                {
                    if (!resp.content.empty()) resp.content += "\n";
                    resp.content += part["text"].get<std::string>();
                }
                else if (part.contains("functionCall") && part["functionCall"].is_object())
                {
                    const auto& fc = part["functionCall"];
                    FToolCall call;
                    if (fc.contains("name") && fc["name"].is_string())
                    {
                        call.name = fc["name"].get<std::string>();
                    }
                    if (fc.contains("args"))
                    {
                        call.argumentsJson = fc["args"].dump();
                    }
                    else
                    {
                        call.argumentsJson = "{}";
                    }
                    // Gemini does not return an id; synthesize one.
                    call.id = "call_" + std::to_string(resp.toolCalls.size());
                    resp.toolCalls.push_back(std::move(call));
                }
            }
        }
        if (body.contains("usageMetadata") && body["usageMetadata"].is_object())
        {
            const auto& u = body["usageMetadata"];
            if (u.contains("promptTokenCount") && u["promptTokenCount"].is_number_integer())
            {
                resp.usage.promptTokens = u["promptTokenCount"].get<int>();
            }
            if (u.contains("candidatesTokenCount") && u["candidatesTokenCount"].is_number_integer())
            {
                resp.usage.completionTokens = u["candidatesTokenCount"].get<int>();
            }
        }
        return resp;
    }

    json BuildOllamaGenerateRequestBody(const FChatRequest& req)
    {
        // Flatten messages into a single prompt (system messages prepended).
        std::string systemText;
        std::string userText;
        for (const auto& m : req.messages)
        {
            if (m.role == EChatRole::System)
            {
                if (!systemText.empty()) systemText += "\n";
                systemText += m.content;
            }
            else if (m.role == EChatRole::User || m.role == EChatRole::Assistant)
            {
                if (!userText.empty()) userText += "\n";
                userText += m.content;
            }
        }
        std::string prompt = systemText.empty() ? userText : (systemText + "\n\n" + userText);
        json body = {
            {"model", req.model},
            {"prompt", prompt},
            {"stream", false},
        };
        return body;
    }

    FChatResponse ParseOllamaGenerateResponse(const json& body)
    {
        if (body.contains("error"))
        {
            std::string msg = body["error"].is_string() ? body["error"].get<std::string>() : body["error"].dump();
            return FChatResponse::Failure(msg);
        }
        if (body.contains("response") && body["response"].is_string())
        {
            return FChatResponse::Success(body["response"].get<std::string>());
        }
        return FChatResponse::Failure("unexpected ollama response format");
    }
}
