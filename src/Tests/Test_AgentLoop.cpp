#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "Modules/NextAI/AI/AgentLoop.hpp"
#include "Modules/NextAI/AI/MainThreadDispatcher.hpp"
#include "Modules/NextAI/AI/ToolRegistry.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using json = nlohmann::json;
using namespace NextAI;

namespace
{
    // Tool that records each invocation and returns a deterministic result.
    class FRecorderTool : public IAITool
    {
    public:
        FRecorderTool(std::string name, std::string description, std::string result,
                      bool mainThread = false, EToolParamType paramType = EToolParamType::String)
            : name_(std::move(name)), description_(std::move(description)),
              result_(std::move(result)), mainThread_(mainThread), paramType_(paramType) {}

        std::string Name() const override { return name_; }
        FToolSchema Schema() const override
        {
            FToolSchema s;
            s.name = name_;
            s.description = description_;
            s.params.push_back({"q", paramType_, "query", false});
            return s;
        }
        bool RequiresMainThread() const override { return mainThread_; }

        std::string Execute(const json& args, FToolCallContext& ctx) override
        {
            calls.push_back(args.dump());
            lastStep = ctx.step;
            lastThreadWasSubmitter = (std::this_thread::get_id() == ownerThread);
            return result_;
        }

        std::vector<std::string> calls;
        int lastStep = 0;
        bool lastThreadWasSubmitter = false;
        std::thread::id ownerThread{};

    private:
        std::string name_, description_, result_;
        bool mainThread_;
        EToolParamType paramType_;
    };

    class FThrowingTool : public IAITool
    {
    public:
        std::string Name() const override { return "boom"; }
        FToolSchema Schema() const override { return {"boom", "throws", {}}; }
        std::string Execute(const json&, FToolCallContext&) override
        {
            throw std::runtime_error("kaboom");
        }
    };

    // Scriptable provider that returns canned responses in order.
    struct FScriptedProvider
    {
        std::vector<FChatResponse> responses;
        int index = 0;
        std::vector<FChatRequest> seen;

        FChatResponse operator()(const FChatRequest& req)
        {
            seen.push_back(req);
            if (index >= static_cast<int>(responses.size()))
            {
                return FChatResponse::Failure("script exhausted");
            }
            return responses[index++];
        }
    };

    FChatResponse MakeToolCallResponse(const std::string& id, const std::string& name,
                                       const std::string& argsJson)
    {
        FChatResponse r;
        r.success = true;
        FToolCall call;
        call.id = id;
        call.name = name;
        call.argumentsJson = argsJson;
        r.toolCalls.push_back(call);
        return r;
    }
}

TEST_CASE("FToolRegistry registers, looks up, and builds schemas", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FRecorderTool>("a", "tool a", "ra"));
    reg.Register(std::make_unique<FRecorderTool>("b", "tool b", "rb"));
    REQUIRE(reg.Size() == 2);
    REQUIRE(reg.Find("a") != nullptr);
    REQUIRE(reg.Find("missing") == nullptr);

    auto schemas = reg.BuildSchemas();
    REQUIRE(schemas.size() == 2);
}

TEST_CASE("FAgentLoop completes a single tool-call chain", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    auto* tool = new FRecorderTool("scene_list_nodes", "list", "node-a, node-b");
    reg.Register(std::unique_ptr<IAITool>(tool));

    FScriptedProvider provider;
    provider.responses.push_back(MakeToolCallResponse("c1", "scene_list_nodes", R"({"q":""})"));
    FChatResponse finalResp;
    finalResp.success = true;
    finalResp.content = "There are 2 nodes: node-a and node-b";
    provider.responses.push_back(finalResp);

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("list nodes"));

    FInMemoryAgentEventSink sink;
    FAgentResult result = FAgentLoop::Run(seed, reg, std::ref(provider), {}, &sink);

    REQUIRE(result.success);
    REQUIRE(result.toolCallsExecuted == 1);
    REQUIRE(result.stepsUsed == 2);
    REQUIRE(result.finalContent.find("node-a") != std::string::npos);
    REQUIRE(tool->calls.size() == 1);

    // Verify schemas were injected into the provider request.
    REQUIRE(provider.seen[0].tools.size() == 1);
    REQUIRE(provider.seen[0].tools[0].name == "scene_list_nodes");

    // Transcript: user, assistant(tool_call), tool, assistant(final)
    REQUIRE(result.transcript.size() == 4);
    REQUIRE(result.transcript[1].role == EChatRole::Assistant);
    REQUIRE(result.transcript[1].toolCalls.size() == 1);
    REQUIRE(result.transcript[2].role == EChatRole::Tool);

    auto events = sink.Drain();
    REQUIRE(events.size() >= 3); // call, result, final
}

TEST_CASE("FAgentLoop reports unknown tool back to the LLM and continues", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FRecorderTool>("known", "k", "ok"));

    FScriptedProvider provider;
    provider.responses.push_back(MakeToolCallResponse("c1", "unknown_tool", "{}"));
    FChatResponse done;
    done.success = true;
    done.content = "giving up";
    provider.responses.push_back(done);

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("do thing"));

    FAgentResult result = FAgentLoop::Run(seed, reg, std::ref(provider));
    REQUIRE(result.success);
    REQUIRE(result.toolCallsExecuted == 0);
    // Tool message should contain ERROR prefix
    bool foundErr = false;
    for (const auto& m : result.transcript)
    {
        if (m.role == EChatRole::Tool && m.content.find("ERROR: unknown tool") != std::string::npos)
        {
            foundErr = true;
        }
    }
    REQUIRE(foundErr);
}

TEST_CASE("FAgentLoop catches tool exceptions and routes them as tool result", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FThrowingTool>());

    FScriptedProvider provider;
    provider.responses.push_back(MakeToolCallResponse("c1", "boom", "{}"));
    FChatResponse done;
    done.success = true;
    done.content = "noted the error";
    provider.responses.push_back(done);

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("trigger"));
    FAgentResult result = FAgentLoop::Run(seed, reg, std::ref(provider));
    REQUIRE(result.success);

    bool found = false;
    for (const auto& m : result.transcript)
    {
        if (m.role == EChatRole::Tool && m.content.find("kaboom") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("FAgentLoop enforces maxSteps and reports overflow", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FRecorderTool>("loop", "l", "again"));

    FScriptedProvider provider;
    for (int i = 0; i < 10; ++i)
    {
        provider.responses.push_back(MakeToolCallResponse("c" + std::to_string(i), "loop", "{}"));
    }

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("forever"));

    FAgentOptions opts;
    opts.maxSteps = 3;
    FAgentResult result = FAgentLoop::Run(seed, reg, std::ref(provider), opts);

    REQUIRE_FALSE(result.success);
    REQUIRE(result.maxStepsExceeded);
    REQUIRE(result.stepsUsed == 3);
    REQUIRE(result.toolCallsExecuted == 3);
}

TEST_CASE("FAgentLoop applies grounding reminder when LLM finalizes without tool use", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FRecorderTool>("scene_list_nodes", "list", "node-a"));

    FScriptedProvider provider;
    FChatResponse premature;
    premature.success = true;
    premature.content = "I think there are some nodes.";
    provider.responses.push_back(premature);

    // After reminder, model calls the tool, then finalizes.
    provider.responses.push_back(MakeToolCallResponse("c1", "scene_list_nodes", R"({"q":""})"));
    FChatResponse done;
    done.success = true;
    done.content = "There is 1 node: node-a";
    provider.responses.push_back(done);

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("list nodes"));

    FAgentOptions opts;
    opts.maxGroundingRetries = 2;
    FAgentResult result = FAgentLoop::Run(seed, reg, std::ref(provider), opts);

    REQUIRE(result.success);
    REQUIRE(result.groundingRetries == 1);
    REQUIRE(result.toolCallsExecuted == 1);
    REQUIRE(result.finalContent.find("node-a") != std::string::npos);
}

TEST_CASE("FAgentLoop honors cancel flag between steps", "[Unit][AI][AgentLoop]")
{
    FToolRegistry reg;
    reg.Register(std::make_unique<FRecorderTool>("t", "t", "ok"));

    std::atomic<bool> cancel{false};
    FScriptedProvider provider;
    provider.responses.push_back(MakeToolCallResponse("c1", "t", "{}"));
    // Cancel after first iteration's tool runs but before next provider call.
    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("go"));

    auto wrapper = [&](const FChatRequest& req) {
        FChatResponse r = provider(req);
        cancel.store(true); // trip cancel before next loop iteration
        return r;
    };

    FAgentResult result = FAgentLoop::Run(seed, reg, wrapper, {}, nullptr, nullptr, &cancel);
    REQUIRE(result.cancelled);
    REQUIRE_FALSE(result.success);
}

TEST_CASE("FAgentLoop parses fallback tool calls from json fence", "[Unit][AI][AgentLoop]")
{
    std::string content =
        "Sure thing, here's the call:\n```json\n"
        R"({"name":"scene_move","arguments":{"node":"crate","x":2.0}})"
        "\n```\n";
    auto calls = FAgentLoop::ParseFallbackToolCalls(content);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].name == "scene_move");
    REQUIRE(calls[0].argumentsJson.find("\"node\":\"crate\"") != std::string::npos);
}

TEST_CASE("FAgentLoop fallback handles bare JSON object and array shapes", "[Unit][AI][AgentLoop]")
{
    auto bare = FAgentLoop::ParseFallbackToolCalls(R"({"tool":"scene_list_nodes","args":{}})");
    REQUIRE(bare.size() == 1);
    REQUIRE(bare[0].name == "scene_list_nodes");

    auto arr = FAgentLoop::ParseFallbackToolCalls(
        R"([{"name":"a","arguments":{}},{"name":"b","arguments":{}}])");
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0].name == "a");
    REQUIRE(arr[1].name == "b");

    auto empty = FAgentLoop::ParseFallbackToolCalls("just text, no tool here");
    REQUIRE(empty.empty());
}

TEST_CASE("FMainThreadDispatcher runs tasks on the draining thread", "[Unit][AI][AgentLoop]")
{
    FMainThreadDispatcher dispatcher;
    std::thread::id mainId = std::this_thread::get_id();
    std::atomic<std::thread::id> seenId{};

    std::thread worker([&]() {
        auto future = dispatcher.Submit([&]() -> std::string {
            seenId = std::this_thread::get_id();
            return "done";
        });
        REQUIRE(future.get() == "done");
    });

    // Spin a few times to let worker enqueue.
    while (dispatcher.QueuedCount() == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(dispatcher.DrainOnce() == 1);
    worker.join();
    REQUIRE(seenId.load() == mainId);
}

TEST_CASE("FAgentLoop marshals RequiresMainThread tools through dispatcher", "[Unit][AI][AgentLoop]")
{
    FMainThreadDispatcher dispatcher;
    FToolRegistry reg;
    auto* tool = new FRecorderTool("scene_select", "select", "selected", /*mainThread=*/true);
    tool->ownerThread = std::this_thread::get_id();
    reg.Register(std::unique_ptr<IAITool>(tool));

    FScriptedProvider provider;
    provider.responses.push_back(MakeToolCallResponse("c1", "scene_select", "{}"));
    FChatResponse done;
    done.success = true;
    done.content = "done";
    provider.responses.push_back(done);

    FChatRequest seed;
    seed.messages.push_back(FChatMessage::User("select"));

    // Run loop on a worker thread and drain dispatcher on main thread.
    std::atomic<bool> finished{false};
    FAgentResult result;
    std::thread runner([&]() {
        result = FAgentLoop::Run(seed, reg, std::ref(provider), {}, nullptr, &dispatcher);
        finished = true;
    });

    while (!finished.load())
    {
        dispatcher.DrainOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runner.join();

    REQUIRE(result.success);
    REQUIRE(tool->calls.size() == 1);
    REQUIRE(tool->lastThreadWasSubmitter);
}
