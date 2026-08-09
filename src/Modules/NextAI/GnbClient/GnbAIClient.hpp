#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAIProcess.hpp"

#include <future>
#include <nlohmann/json.hpp>

namespace NextAI
{
    struct FGnbAIEvent
    {
        std::string type;
        std::string runId;
        int sequence = 0;
        nlohmann::json payload;
    };

    class FGnbAIClient
    {
    public:
        FGnbAIClient();
        ~FGnbAIClient();
        FGnbAIClient(const FGnbAIClient&) = delete;
        FGnbAIClient& operator=(const FGnbAIClient&) = delete;

        bool Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                   std::string& error);
        bool StartDefault(const std::filesystem::path& repoRoot, std::string& error);
        void Stop();
        bool IsAvailable() const;

        std::future<nlohmann::json> Request(const std::string& method,
                                            const nlohmann::json& params = nlohmann::json::object());
        std::future<nlohmann::json> CreateSession(const std::string& profile,
                                                  const std::string& provider = {},
                                                  const std::string& model = {});
        std::future<nlohmann::json> Chat(const std::string& sessionId,
                                         const std::vector<nlohmann::json>& messages,
                                         const std::string& runId = {});
        std::future<nlohmann::json> Cancel(const std::string& runId);

        std::vector<FGnbAIEvent> DrainEvents();

        static bool ValidateProtocolFrame(const nlohmann::json& frame, std::string& error);

    private:
        struct FPendingRequest { std::promise<nlohmann::json> promise; };

        void ReaderLoop();
        void HandleFrame(const nlohmann::json& frame);
        void FailPending(const std::string& message);
        bool WriteFrame(const nlohmann::json& frame);

        FGnbAIProcess process_;
        std::thread readerThread_;
        std::atomic<bool> stopping_ = false;
        std::atomic<uint64_t> nextRequestId_ = 1;
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<FPendingRequest>> pending_;
        std::vector<FGnbAIEvent> events_;
    };
}
