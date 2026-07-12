#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentClient.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"

#include <cstdlib>

namespace NextAI
{
    namespace
    {
        std::future<nlohmann::json> FailedFuture(const std::string& message)
        {
            std::promise<nlohmann::json> promise;
            promise.set_exception(std::make_exception_ptr(std::runtime_error(message)));
            return promise.get_future();
        }
    }

    FGnbAgentClient::FGnbAgentClient() = default;
    FGnbAgentClient::~FGnbAgentClient() { Stop(); }

    bool FGnbAgentClient::Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                                std::string& error)
    {
        Stop(); stopping_ = false;
        if (!process_.Start(executable, repoRoot, error)) return false;
        readerThread_ = std::thread(&FGnbAgentClient::ReaderLoop, this);
        try
        {
            const nlohmann::json response = Request("initialize", {
                {"protocolVersion", 1}, {"client", {{"name", "gkNextEngine"}, {"version", "1"}}},
                {"capabilities", {{"remoteTools", true}}}
            }).get();
            if (response.value("protocolVersion", 0) != 1) throw std::runtime_error("bridge protocol mismatch");
        }
        catch (const std::exception& exception)
        {
            error = exception.what(); Stop(); return false;
        }
        return true;
    }

    bool FGnbAgentClient::StartDefault(const std::filesystem::path& repoRoot, std::string& error)
    {
        std::vector<std::filesystem::path> candidates;
        if (const char* configured = std::getenv("GNB_EXECUTABLE")) candidates.emplace_back(configured);
#if WIN32
		candidates.emplace_back(repoRoot / "gnb.exe");
		candidates.emplace_back(NextRenderer::GetExecutableDirectory() / "gnb.exe");
#else
		candidates.emplace_back(repoRoot / "gnb");
		candidates.emplace_back(NextRenderer::GetExecutableDirectory() / "gnb");
#endif
        for (const auto& candidate : candidates)
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) return Start(candidate, repoRoot, error);
        }
        error = "gnb bridge unavailable; checked GNB_EXECUTABLE and repository root";
        return false;
    }

    void FGnbAgentClient::Stop()
    {
        stopping_ = true;
        process_.Stop();
        if (readerThread_.joinable() && readerThread_.get_id() != std::this_thread::get_id()) readerThread_.join();
        FailPending("gnb bridge stopped");
    }
    bool FGnbAgentClient::IsAvailable() const { return !stopping_ && process_.IsRunning(); }

    std::future<nlohmann::json> FGnbAgentClient::Request(const std::string& method, const nlohmann::json& params)
    {
        if (!process_.IsRunning()) return FailedFuture("gnb bridge is not running");
        const std::string id = std::to_string(nextRequestId_++);
        auto pending = std::make_shared<FPendingRequest>();
        auto future = pending->promise.get_future();
        { std::lock_guard lock(mutex_); pending_[id] = pending; }
        if (!WriteFrame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}))
        {
            std::lock_guard lock(mutex_); pending_.erase(id);
            pending->promise.set_exception(std::make_exception_ptr(std::runtime_error("failed to write bridge request")));
        }
        return future;
    }
    std::future<nlohmann::json> FGnbAgentClient::CreateSession(const std::string& profile,
                                                               const std::string& provider,
                                                               const std::string& model)
    { return Request("session.create", {{"profile", profile}, {"provider", provider}, {"model", model}}); }
    std::future<nlohmann::json> FGnbAgentClient::Chat(const std::string& sessionId,
                                                      const std::vector<nlohmann::json>& messages,
                                                      const std::string& runId)
    { return Request("llm.chat", {{"sessionId", sessionId}, {"messages", messages}, {"runId", runId}}); }
    std::future<nlohmann::json> FGnbAgentClient::RunAgent(const std::string& sessionId, const std::string& prompt,
                                                          const std::string& runId)
    { return Request("agent.run", {{"sessionId", sessionId}, {"prompt", prompt}, {"runId", runId}}); }
    std::future<nlohmann::json> FGnbAgentClient::Cancel(const std::string& runId)
    { return Request("run.cancel", {{"runId", runId}}); }

    std::future<nlohmann::json> FGnbAgentClient::RegisterTools(std::vector<FGnbRemoteTool> tools)
    {
        nlohmann::json descriptors = nlohmann::json::array();
        { std::lock_guard lock(mutex_); for (auto& tool : tools) { descriptors.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}, {"mutating", tool.mutating}}); tools_[tool.name] = std::move(tool); } }
        return Request("tools.register", {{"tools", descriptors}});
    }

    void FGnbAgentClient::PumpEvents()
    {
        std::vector<FToolInvocation> queue;
        { std::lock_guard lock(mutex_); queue.swap(toolQueue_); }
        for (const auto& invocation : queue)
        {
            nlohmann::json response{{"jsonrpc", "2.0"}, {"id", invocation.requestId}};
            try
            {
                FGnbRemoteTool tool;
                { std::lock_guard lock(mutex_); auto it = tools_.find(invocation.params.value("name", "")); if (it == tools_.end()) throw std::runtime_error("unknown remote tool"); tool = it->second; }
                response["result"] = {{"callId", invocation.params.value("callId", "")}, {"content", tool.handler(invocation.params.value("arguments", nlohmann::json::object()))}};
            }
            catch (const std::exception& exception)
            { response["error"] = {{"code", -32007}, {"message", exception.what()}, {"data", {{"category", "tool_error"}, {"retryable", false}}}}; }
            WriteFrame(response);
        }
    }

    std::vector<FGnbAgentEvent> FGnbAgentClient::DrainEvents()
    { std::lock_guard lock(mutex_); std::vector<FGnbAgentEvent> result; result.swap(events_); return result; }

    void FGnbAgentClient::ReaderLoop()
    {
        std::string line;
        while (!stopping_ && process_.ReadLine(line))
        {
            try { HandleFrame(nlohmann::json::parse(line)); }
            catch (const std::exception& exception) { FailPending(std::string("invalid bridge frame: ") + exception.what()); break; }
        }
        if (!stopping_) FailPending("gnb bridge exited");
    }

    void FGnbAgentClient::HandleFrame(const nlohmann::json& frame)
    {
        std::string validationError;
        if (!ValidateProtocolFrame(frame, validationError)) throw std::runtime_error(validationError);
        if (frame.contains("method"))
        {
            const std::string method = frame.value("method", "");
            if (method == "run.event")
            {
                const auto params = frame.value("params", nlohmann::json::object());
                std::lock_guard lock(mutex_); events_.push_back({params.value("type", ""), params.value("runId", ""), params.value("sequence", 0), params});
            }
            else if (method == "tool.execute")
            { std::lock_guard lock(mutex_); toolQueue_.push_back({frame.at("id").get<std::string>(), frame.value("params", nlohmann::json::object())}); }
            return;
        }
        const std::string id = frame.at("id").get<std::string>();
        std::shared_ptr<FPendingRequest> pending;
        { std::lock_guard lock(mutex_); auto it = pending_.find(id); if (it == pending_.end()) return; pending = it->second; pending_.erase(it); }
        if (frame.contains("error")) pending->promise.set_exception(std::make_exception_ptr(std::runtime_error(frame["error"].value("message", "bridge error"))));
        else pending->promise.set_value(frame.value("result", nlohmann::json::object()));
    }

    void FGnbAgentClient::FailPending(const std::string& message)
    {
        std::unordered_map<std::string, std::shared_ptr<FPendingRequest>> pending;
        { std::lock_guard lock(mutex_); pending.swap(pending_); }
        for (auto& [id, request] : pending) request->promise.set_exception(std::make_exception_ptr(std::runtime_error(message)));
    }
    bool FGnbAgentClient::WriteFrame(const nlohmann::json& frame) { return process_.WriteLine(frame.dump()); }
    bool FGnbAgentClient::ValidateProtocolFrame(const nlohmann::json& frame, std::string& error)
    {
        if (!frame.is_object() || frame.value("jsonrpc", "") != "2.0") { error = "expected JSON-RPC 2.0 object"; return false; }
        if (!frame.contains("id") && !frame.contains("method")) { error = "frame requires id or method"; return false; }
        if (frame.contains("id") && !frame["id"].is_string()) { error = "frame id must be a string"; return false; }
        return true;
    }
}
