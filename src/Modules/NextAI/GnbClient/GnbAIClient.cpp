#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAIClient.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"

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

    FGnbAIClient::FGnbAIClient() = default;
    FGnbAIClient::~FGnbAIClient() { Stop(); }

    bool FGnbAIClient::Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                                std::string& error)
    {
        Stop(); stopping_ = false;
        if (!process_.Start(executable, repoRoot, error)) return false;
        readerThread_ = std::thread(&FGnbAIClient::ReaderLoop, this);
        try
        {
            const nlohmann::json response = Request("initialize", {
                {"protocolVersion", 2}, {"client", {{"name", "gkNextEngine"}, {"version", "2"}}},
                {"capabilities", {{"streaming", true}, {"structuredOutput", true}}}
            }).get();
            if (response.value("protocolVersion", 0) != 2) throw std::runtime_error("bridge protocol mismatch");
        }
        catch (const std::exception& exception)
        {
            error = exception.what(); Stop(); return false;
        }
        return true;
    }

    bool FGnbAIClient::StartDefault(const std::filesystem::path& repoRoot, std::string& error)
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

    void FGnbAIClient::Stop()
    {
        stopping_ = true;
        process_.Stop();
        if (readerThread_.joinable() && readerThread_.get_id() != std::this_thread::get_id()) readerThread_.join();
        FailPending("gnb bridge stopped");
    }
    bool FGnbAIClient::IsAvailable() const { return !stopping_ && process_.IsRunning(); }

    std::future<nlohmann::json> FGnbAIClient::Request(const std::string& method, const nlohmann::json& params)
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
    std::future<nlohmann::json> FGnbAIClient::CreateSession(const std::string& profile,
                                                               const std::string& provider,
                                                               const std::string& model)
    { return Request("session.create", {{"profile", profile}, {"provider", provider}, {"model", model}}); }
    std::future<nlohmann::json> FGnbAIClient::Chat(const std::string& sessionId,
                                                      const std::vector<nlohmann::json>& messages,
                                                      const std::string& runId)
    { return Request("llm.chat", {{"sessionId", sessionId}, {"messages", messages}, {"runId", runId}}); }
    std::future<nlohmann::json> FGnbAIClient::Cancel(const std::string& runId)
    { return Request("run.cancel", {{"runId", runId}}); }

    std::vector<FGnbAIEvent> FGnbAIClient::DrainEvents()
    { std::lock_guard lock(mutex_); std::vector<FGnbAIEvent> result; result.swap(events_); return result; }

    void FGnbAIClient::ReaderLoop()
    {
        std::string line;
        while (!stopping_ && process_.ReadLine(line))
        {
            try { HandleFrame(nlohmann::json::parse(line)); }
            catch (const std::exception& exception) { FailPending(std::string("invalid bridge frame: ") + exception.what()); break; }
        }
        if (!stopping_) FailPending("gnb bridge exited");
    }

    void FGnbAIClient::HandleFrame(const nlohmann::json& frame)
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
            return;
        }
        const std::string id = frame.at("id").get<std::string>();
        std::shared_ptr<FPendingRequest> pending;
        { std::lock_guard lock(mutex_); auto it = pending_.find(id); if (it == pending_.end()) return; pending = it->second; pending_.erase(it); }
        if (frame.contains("error")) pending->promise.set_exception(std::make_exception_ptr(std::runtime_error(frame["error"].value("message", "bridge error"))));
        else pending->promise.set_value(frame.value("result", nlohmann::json::object()));
    }

    void FGnbAIClient::FailPending(const std::string& message)
    {
        std::unordered_map<std::string, std::shared_ptr<FPendingRequest>> pending;
        { std::lock_guard lock(mutex_); pending.swap(pending_); }
        for (auto& [id, request] : pending) request->promise.set_exception(std::make_exception_ptr(std::runtime_error(message)));
    }
    bool FGnbAIClient::WriteFrame(const nlohmann::json& frame) { return process_.WriteLine(frame.dump()); }
    bool FGnbAIClient::ValidateProtocolFrame(const nlohmann::json& frame, std::string& error)
    {
        if (!frame.is_object() || frame.value("jsonrpc", "") != "2.0") { error = "expected JSON-RPC 2.0 object"; return false; }
        if (!frame.contains("id") && !frame.contains("method")) { error = "frame requires id or method"; return false; }
        if (frame.contains("id") && !frame["id"].is_string()) { error = "frame id must be a string"; return false; }
        return true;
    }
}
