#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/NextAIModule.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentClient.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"

namespace NextAI
{
    namespace
    {
        constexpr const char* kServiceKey = "NextAI.FAIService";
        constexpr const char* kGnbClientKey = "NextAI.FGnbAgentClient";
        constexpr const char* kGnbClientErrorKey = "NextAI.FGnbAgentClient.Error";

        std::filesystem::path FindRepoRoot()
        {
            std::error_code error;
            auto current = std::filesystem::current_path(error);
            for (int i = 0; i < 8 && !current.empty(); ++i)
            {
                if (std::filesystem::exists(current / "gnb.toml", error)) return current;
                const auto parent = current.parent_path(); if (parent == current) break; current = parent;
            }
            current = NextRenderer::GetExecutableDirectory();
            for (int i = 0; i < 8 && !current.empty(); ++i)
            {
                if (std::filesystem::exists(current / "gnb.toml", error)) return current;
                const auto parent = current.parent_path(); if (parent == current) break; current = parent;
            }
            return {};
        }
    }

    FAIService* GetAIService(NextEngine& engine)
    {
        if (auto existing = engine.GetExternalService(kServiceKey))
        {
            return static_cast<FAIService*>(existing.get());
        }
        auto service = std::make_shared<FAIService>();
        FAIService* raw = service.get();
        engine.SetExternalService(kServiceKey, std::move(service));
        return raw;
    }

    FGnbAgentClient* GetGnbAgentClient(NextEngine& engine)
    {
        if (auto existing = engine.GetExternalService(kGnbClientKey)) return static_cast<FGnbAgentClient*>(existing.get());
        auto client = std::make_shared<FGnbAgentClient>();
        const auto repoRoot = FindRepoRoot();
        std::string error;
        if (repoRoot.empty() || !client->StartDefault(repoRoot, error))
        {
            if (error.empty()) error = "could not discover repository root for gnb bridge";
            engine.SetExternalService(kGnbClientErrorKey, std::make_shared<std::string>(error));
            SPDLOG_WARN("gnb bridge unavailable: {}", error);
        }
        FGnbAgentClient* raw = client.get(); engine.SetExternalService(kGnbClientKey, std::move(client)); return raw;
    }

    const std::string& GetGnbAgentClientError(NextEngine& engine)
    {
        static const std::string empty;
        if (auto existing = engine.GetExternalService(kGnbClientErrorKey)) return *static_cast<std::string*>(existing.get());
        return empty;
    }
}
