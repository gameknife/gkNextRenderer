#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace NextAI
{
    enum class EChatRole
    {
        System,
        User,
        Assistant,
        Tool
    };

    struct FToolCall
    {
        std::string id;
        std::string name;
        std::string argumentsJson;
    };

    struct FChatMessage
    {
        EChatRole role = EChatRole::User;
        std::string content;
        std::string name;
        std::string toolCallId;
        std::vector<FToolCall> toolCalls;

        static FChatMessage System(std::string text)
        {
            FChatMessage m;
            m.role = EChatRole::System;
            m.content = std::move(text);
            return m;
        }
        static FChatMessage User(std::string text)
        {
            FChatMessage m;
            m.role = EChatRole::User;
            m.content = std::move(text);
            return m;
        }
        static FChatMessage Assistant(std::string text)
        {
            FChatMessage m;
            m.role = EChatRole::Assistant;
            m.content = std::move(text);
            return m;
        }
        static FChatMessage ToolResult(std::string toolCallId, std::string toolName, std::string text)
        {
            FChatMessage m;
            m.role = EChatRole::Tool;
            m.toolCallId = std::move(toolCallId);
            m.name = std::move(toolName);
            m.content = std::move(text);
            return m;
        }
    };

    enum class EToolParamType
    {
        String,
        Number,
        Integer,
        Boolean,
        Object,
        Array
    };

    struct FToolParam
    {
        std::string name;
        EToolParamType type = EToolParamType::String;
        std::string description;
        bool required = false;
    };

    struct FToolSchema
    {
        std::string name;
        std::string description;
        std::vector<FToolParam> params;
    };

    struct FChatRequest
    {
        std::vector<FChatMessage> messages;
        std::vector<FToolSchema> tools;
        std::string model;
        float temperature = 0.7f;
        int maxTokens = 0;
        // Whether the model should emit a reasoning/<think> block. Default off so
        // reasoning never leaks into content or confuses tool-call parsing. Only
        // honored by backends that accept chat_template_kwargs (llama-server).
        bool enableThinking = false;
    };

    struct FChatUsage
    {
        int promptTokens = 0;
        int completionTokens = 0;
    };

    struct FChatResponse
    {
        bool success = false;
        std::string content;
        std::vector<FToolCall> toolCalls;
        std::string finishReason;
        std::string errorMessage;
        FChatUsage usage;

        static FChatResponse Success(std::string c)
        {
            FChatResponse r;
            r.success = true;
            r.content = std::move(c);
            return r;
        }
        static FChatResponse Failure(std::string err)
        {
            FChatResponse r;
            r.success = false;
            r.errorMessage = std::move(err);
            return r;
        }
    };

    // Serializers for OpenAI-compatible /v1/chat/completions endpoints
    // (Zhipu, DeepSeek, llama-server, Ollama-with-tools).
    nlohmann::json BuildOpenAIChatRequestBody(const FChatRequest& req, bool injectThinkingControl = false);
    FChatResponse ParseOpenAIChatResponse(const nlohmann::json& body);

    // Serializers for Gemini generateContent.
    nlohmann::json BuildGeminiChatRequestBody(const FChatRequest& req);
    FChatResponse ParseGeminiChatResponse(const nlohmann::json& body);

    // Legacy Ollama /api/generate (no tool calling, single-turn).
    nlohmann::json BuildOllamaGenerateRequestBody(const FChatRequest& req);
    FChatResponse ParseOllamaGenerateResponse(const nlohmann::json& body);

    const char* ToolParamTypeToString(EToolParamType t);
    const char* ChatRoleToString(EChatRole r);
}
