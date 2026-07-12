#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentClient.hpp"

#include "Engine/Runtime/Platform/PlatformCommon.h"

#include <nlohmann/json.hpp>

namespace NextAI
{
    namespace
    {
        std::filesystem::path FindRepoRoot()
        {
            std::error_code error;
            for (auto start : {std::filesystem::current_path(error), NextRenderer::GetExecutableDirectory()})
            {
                for (int i = 0; i < 8 && !start.empty(); ++i)
                {
                    if (std::filesystem::exists(start / "gnb.toml", error)) return start;
                    const auto parent = start.parent_path(); if (parent == start) break; start = parent;
                }
            }
            return {};
        }
    }

    FAIService::FAIService() { LoadConfig(); }
    FAIService::FAIService(std::string) { LoadConfig(); }
    FAIService::~FAIService()
    {
        std::lock_guard lock(asyncThreadsMutex_);
        for (auto& thread : asyncThreads_) thread.request_stop();
        for (auto& thread : asyncThreads_) if (thread.joinable()) thread.join();
        if (client_) client_->Stop();
    }

    bool FAIService::LoadConfig()
    {
        if (client_ && client_->IsAvailable()) return true;
        const auto repoRoot = FindRepoRoot();
        if (repoRoot.empty()) { statusMessage_ = "gnb repository root not found"; return false; }
        client_ = std::make_unique<FGnbAgentClient>();
        std::string error;
        if (!client_->StartDefault(repoRoot, error))
        {
            client_.reset(); configured_ = false; status_ = EAIStatus::NotConfigured; statusMessage_ = error; return false;
        }
        if (!RefreshCatalog() || !RecreateSession())
        {
            client_->Stop(); client_.reset(); configured_ = false; status_ = EAIStatus::NotConfigured; return false;
        }
        configured_ = true; status_ = EAIStatus::Ready; statusMessage_ = currentProviderId_ + " ready via gnb";
        return true;
    }

    bool FAIService::RefreshCatalog()
    {
        try
        {
            providers_.clear();
            for (const auto& item : client_->Request("providers.list").get())
            {
                FAIProviderDescriptor descriptor;
                descriptor.id = item.value("id", ""); descriptor.displayName = item.value("displayName", descriptor.id);
                descriptor.kind = item.value("kind", ""); descriptor.defaultModel = item.value("defaultModel", "");
                descriptor.configured = item.value("configured", false); descriptor.available = item.value("available", false);
                if (item.contains("models")) descriptor.models = item["models"].get<std::vector<std::string>>();
                providers_.push_back(std::move(descriptor));
            }
            const auto profiles = client_->Request("profiles.list").get();
            if (profiles.contains(currentProfileId_))
            {
                currentProviderId_ = profiles[currentProfileId_].value("provider", currentProviderId_);
                currentModelId_ = profiles[currentProfileId_].value("model", currentModelId_);
            }
            if (currentProviderId_.empty() && !providers_.empty()) currentProviderId_ = providers_.front().id;
            if (currentModelId_.empty())
            {
                const auto selected = std::find_if(providers_.begin(), providers_.end(), [this](const auto& item) { return item.id == currentProviderId_; });
                if (selected != providers_.end()) currentModelId_ = selected->defaultModel;
            }
            return true;
        }
        catch (const std::exception& exception) { statusMessage_ = exception.what(); return false; }
    }

    bool FAIService::RecreateSession()
    {
        try
        {
            const auto session = client_->CreateSession(currentProfileId_, currentProviderId_, currentModelId_).get();
            sessionId_ = session.value("id", "");
            currentProviderId_ = session.value("providerId", currentProviderId_);
            currentModelId_ = session.value("modelId", currentModelId_);
            return !sessionId_.empty();
        }
        catch (const std::exception& exception) { statusMessage_ = exception.what(); return false; }
    }

    bool FAIService::SwitchProvider(const std::string& providerId)
    {
        if (status_ == EAIStatus::Generating || !IsProviderConfigured(providerId)) return false;
        currentProviderId_ = providerId; currentModelId_.clear();
        const auto selected = std::find_if(providers_.begin(), providers_.end(), [&providerId](const auto& item) { return item.id == providerId; });
        if (selected != providers_.end()) currentModelId_ = selected->defaultModel;
        return RecreateSession();
    }
    bool FAIService::IsProviderConfigured(const std::string& providerId) const
    {
        const auto found = std::find_if(providers_.begin(), providers_.end(), [&providerId](const auto& item) { return item.id == providerId; });
        return found != providers_.end() && found->configured && found->available;
    }
    std::vector<std::string> FAIService::GetProviderModels(const std::string& providerId) const
    {
        const auto found = std::find_if(providers_.begin(), providers_.end(), [&providerId](const auto& item) { return item.id == providerId; });
        return found == providers_.end() ? std::vector<std::string>{} : found->models;
    }
    bool FAIService::SetCurrentModel(std::string model)
    {
        if (model.empty() || status_ == EAIStatus::Generating) return false;
        currentModelId_ = std::move(model); return RecreateSession();
    }
    bool FAIService::SetProfile(std::string profileId)
    {
        if (profileId.empty() || status_ == EAIStatus::Generating) return false;
        currentProfileId_ = std::move(profileId);
        if (!RefreshCatalog()) return false;
        return RecreateSession();
    }

    FChatResponse FAIService::ChatViaGnb(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_ || !client_ || !client_->IsAvailable()) return FChatResponse::Failure("gnb bridge unavailable");
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& message : request.messages) messages.push_back({{"role", ChatRoleToString(message.role)}, {"content", message.content}});
        static std::atomic<uint64_t> nextRun{1};
        const std::string runId = fmt::format("chat-{}", nextRun++);
        try
        {
            auto future = client_->Request("llm.chat", {{"sessionId", sessionId_}, {"runId", runId}, {"messages", messages},
                {"maxOutputTokens", request.maxTokens}, {"model", request.model}});
            while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready)
            {
                if (onDelta) for (const auto& event : client_->DrainEvents())
                    if (event.runId == runId && event.type == "content.delta") onDelta(event.payload.value("content", ""));
            }
            const auto result = future.get();
            FChatResponse response = FChatResponse::Success(result.value("content", ""));
            response.finishReason = result.value("finishReason", "");
            if (result.contains("usage")) { response.usage.promptTokens = result["usage"].value("promptTokens", 0); response.usage.completionTokens = result["usage"].value("completionTokens", 0); }
            return response;
        }
        catch (const std::exception& exception) { return FChatResponse::Failure(exception.what()); }
    }

    FChatResponse FAIService::Chat(const FChatRequest& request)
    {
        status_ = EAIStatus::Generating; statusMessage_ = "Generating...";
        auto response = ChatViaGnb(request);
        status_ = response.success ? EAIStatus::Ready : EAIStatus::Error;
        statusMessage_ = response.success ? "Ready" : response.errorMessage;
        return response;
    }
    FChatResponse FAIService::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        status_ = EAIStatus::Generating; statusMessage_ = "Generating...";
        auto response = ChatViaGnb(request, std::move(onDelta));
        status_ = response.success ? EAIStatus::Ready : EAIStatus::Error;
        statusMessage_ = response.success ? "Ready" : response.errorMessage;
        return response;
    }
    FAIResponse FAIService::GenerateText(const std::string& prompt)
    {
        const auto started = std::chrono::steady_clock::now(); FChatRequest request; request.messages.push_back(FChatMessage::User(prompt));
        const auto response = Chat(request); const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return response.success ? FAIResponse::Success(response.content, elapsed) : FAIResponse::Failure(response.errorMessage, elapsed);
    }
    void FAIService::GenerateTextAsync(const std::string& prompt, std::function<void(FAIResponse)> callback)
    {
        std::lock_guard lock(asyncThreadsMutex_);
        asyncThreads_.emplace_back([this, prompt, callback = std::move(callback)](std::stop_token) { auto response = GenerateText(prompt); if (callback) callback(std::move(response)); });
    }
}
