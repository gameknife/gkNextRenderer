#pragma once
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <string>
#include <vector>

namespace NextAI
{
    enum class EChatRole
    {
        System,
        User,
        Assistant
    };

    struct FChatMessage
    {
        EChatRole role = EChatRole::User;
        std::string content;

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
    };

    struct FChatRequest
    {
        std::vector<FChatMessage> messages;
        std::string model;
        float temperature = 0.7f;
        int maxTokens = 0;
        // Whether the model should emit a reasoning/<think> block. Default off so
        // reasoning never leaks into content or confuses tool-call parsing. Only
        // honored by backends that accept chat_template_kwargs (llama-server).
        bool enableThinking = false;
        // Optional structured-output contract. jsonSchema must contain a JSON
        // Schema object when responseFormat is Schema.
        enum class EResponseFormat { Text, Json, Schema } responseFormat = EResponseFormat::Text;
        std::string responseSchemaName;
        std::string jsonSchema;
        bool strictSchema = true;
        int deadlineMs = 0;
        bool stateless = false;
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
        std::string finishReason;
        std::string errorMessage;
        FChatUsage usage;
        std::string structuredOutputMode;

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

    using FChatStreamCallback = std::function<void(const std::string& delta)>;

    const char* ChatRoleToString(EChatRole r);
}
