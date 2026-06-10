#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "Modules/NextAI/AI/AIChat.hpp"

using json = nlohmann::json;
using namespace NextAI;

namespace
{
    FToolSchema MakeMoveSchema()
    {
        FToolSchema s;
        s.name = "scene_move";
        s.description = "Translate a node";
        s.params.push_back({"node", EToolParamType::String, "node name or id", true});
        s.params.push_back({"x", EToolParamType::Number, "x offset", true});
        s.params.push_back({"y", EToolParamType::Number, "y offset", false});
        return s;
    }
}

TEST_CASE("BuildOpenAIChatRequestBody emits messages, model and tools", "[Unit][AI][Protocol]")
{
    FChatRequest req;
    req.model = "gemma-4-e4b";
    req.temperature = 0.5f;
    req.maxTokens = 256;
    req.messages.push_back(FChatMessage::System("you are an editor agent"));
    req.messages.push_back(FChatMessage::User("move crate east"));
    req.tools.push_back(MakeMoveSchema());

    json body = BuildOpenAIChatRequestBody(req);

    REQUIRE(body["model"] == "gemma-4-e4b");
    REQUIRE(body["temperature"] == 0.5f);
    REQUIRE(body["max_tokens"] == 256);
    REQUIRE(body["stream"] == false);

    REQUIRE(body["messages"].size() == 2);
    REQUIRE(body["messages"][0]["role"] == "system");
    REQUIRE(body["messages"][1]["role"] == "user");
    REQUIRE(body["messages"][1]["content"] == "move crate east");

    REQUIRE(body.contains("tools"));
    REQUIRE(body["tools"].size() == 1);
    const auto& fn = body["tools"][0]["function"];
    REQUIRE(fn["name"] == "scene_move");
    REQUIRE(fn["parameters"]["type"] == "object");
    REQUIRE(fn["parameters"]["properties"]["node"]["type"] == "string");
    REQUIRE(fn["parameters"]["properties"]["x"]["type"] == "number");
    auto required = fn["parameters"]["required"].get<std::vector<std::string>>();
    REQUIRE(required.size() == 2);
    REQUIRE(std::find(required.begin(), required.end(), "node") != required.end());
    REQUIRE(std::find(required.begin(), required.end(), "x") != required.end());
}

TEST_CASE("BuildOpenAIChatRequestBody round-trips assistant tool calls and tool results", "[Unit][AI][Protocol]")
{
    FChatRequest req;
    FChatMessage assistant;
    assistant.role = EChatRole::Assistant;
    FToolCall call;
    call.id = "call_1";
    call.name = "scene_move";
    call.argumentsJson = R"({"node":"crate","x":2.0})";
    assistant.toolCalls.push_back(call);
    req.messages.push_back(assistant);
    req.messages.push_back(FChatMessage::ToolResult("call_1", "scene_move", "moved 1 node"));

    json body = BuildOpenAIChatRequestBody(req);
    const auto& m0 = body["messages"][0];
    REQUIRE(m0["role"] == "assistant");
    REQUIRE(m0["tool_calls"].size() == 1);
    REQUIRE(m0["tool_calls"][0]["id"] == "call_1");
    REQUIRE(m0["tool_calls"][0]["function"]["name"] == "scene_move");
    REQUIRE(m0["tool_calls"][0]["function"]["arguments"] == R"({"node":"crate","x":2.0})");

    const auto& m1 = body["messages"][1];
    REQUIRE(m1["role"] == "tool");
    REQUIRE(m1["tool_call_id"] == "call_1");
    REQUIRE(m1["name"] == "scene_move");
    REQUIRE(m1["content"] == "moved 1 node");
}

TEST_CASE("ParseOpenAIChatResponse extracts content, tool calls and usage", "[Unit][AI][Protocol]")
{
    json body = R"({
        "choices":[{
            "finish_reason":"tool_calls",
            "message":{
                "content":"calling scene_move",
                "tool_calls":[{
                    "id":"call_42",
                    "type":"function",
                    "function":{"name":"scene_move","arguments":"{\"node\":\"crate\",\"x\":2.0}"}
                }]
            }
        }],
        "usage":{"prompt_tokens":120,"completion_tokens":35}
    })"_json;

    FChatResponse resp = ParseOpenAIChatResponse(body);
    REQUIRE(resp.success);
    REQUIRE(resp.content == "calling scene_move");
    REQUIRE(resp.finishReason == "tool_calls");
    REQUIRE(resp.toolCalls.size() == 1);
    REQUIRE(resp.toolCalls[0].id == "call_42");
    REQUIRE(resp.toolCalls[0].name == "scene_move");
    REQUIRE(resp.toolCalls[0].argumentsJson == R"({"node":"crate","x":2.0})");
    REQUIRE(resp.usage.promptTokens == 120);
    REQUIRE(resp.usage.completionTokens == 35);
}

TEST_CASE("ParseOpenAIChatResponse handles object-form arguments and surfaces errors", "[Unit][AI][Protocol]")
{
    json objArgs = R"({
        "choices":[{"message":{"tool_calls":[{
            "id":"c1","type":"function",
            "function":{"name":"scene_move","arguments":{"node":"crate","x":2}}
        }]}}]
    })"_json;
    FChatResponse a = ParseOpenAIChatResponse(objArgs);
    REQUIRE(a.success);
    REQUIRE(a.toolCalls.size() == 1);
    REQUIRE(a.toolCalls[0].argumentsJson.find("\"node\":\"crate\"") != std::string::npos);

    json err = R"({"error":{"message":"rate limited","type":"quota"}})"_json;
    FChatResponse e = ParseOpenAIChatResponse(err);
    REQUIRE_FALSE(e.success);
    REQUIRE(e.errorMessage == "rate limited");
}

TEST_CASE("BuildGeminiChatRequestBody promotes system message and translates tools", "[Unit][AI][Protocol]")
{
    FChatRequest req;
    req.messages.push_back(FChatMessage::System("be terse"));
    req.messages.push_back(FChatMessage::User("hello"));
    req.tools.push_back(MakeMoveSchema());

    json body = BuildGeminiChatRequestBody(req);
    REQUIRE(body.contains("systemInstruction"));
    REQUIRE(body["systemInstruction"]["parts"][0]["text"] == "be terse");
    REQUIRE(body["contents"].size() == 1);
    REQUIRE(body["contents"][0]["role"] == "user");
    REQUIRE(body["contents"][0]["parts"][0]["text"] == "hello");

    REQUIRE(body["tools"][0]["functionDeclarations"][0]["name"] == "scene_move");
    REQUIRE(body["tools"][0]["functionDeclarations"][0]["parameters"]["properties"]["x"]["type"] == "number");
}

TEST_CASE("ParseGeminiChatResponse parses functionCall parts and synthesizes ids", "[Unit][AI][Protocol]")
{
    json body = R"({
        "candidates":[{
            "finishReason":"STOP",
            "content":{"parts":[
                {"text":"thinking..."},
                {"functionCall":{"name":"scene_move","args":{"node":"crate","x":2.0}}}
            ]}
        }],
        "usageMetadata":{"promptTokenCount":50,"candidatesTokenCount":12}
    })"_json;

    FChatResponse resp = ParseGeminiChatResponse(body);
    REQUIRE(resp.success);
    REQUIRE(resp.content == "thinking...");
    REQUIRE(resp.toolCalls.size() == 1);
    REQUIRE(resp.toolCalls[0].name == "scene_move");
    REQUIRE_FALSE(resp.toolCalls[0].id.empty());
    REQUIRE(resp.toolCalls[0].argumentsJson.find("\"node\":\"crate\"") != std::string::npos);
    REQUIRE(resp.usage.promptTokens == 50);
    REQUIRE(resp.usage.completionTokens == 12);
}

TEST_CASE("BuildOllamaGenerateRequestBody flattens system+user prompts", "[Unit][AI][Protocol]")
{
    FChatRequest req;
    req.model = "llama3.2";
    req.messages.push_back(FChatMessage::System("be terse"));
    req.messages.push_back(FChatMessage::User("ping"));

    json body = BuildOllamaGenerateRequestBody(req);
    REQUIRE(body["model"] == "llama3.2");
    REQUIRE(body["stream"] == false);
    std::string prompt = body["prompt"].get<std::string>();
    REQUIRE(prompt.find("be terse") != std::string::npos);
    REQUIRE(prompt.find("ping") != std::string::npos);
}
