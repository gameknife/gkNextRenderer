#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAIClient.hpp"
#include "Modules/NextAI/AIService.hpp"

#include <catch2/catch_all.hpp>
#include <fstream>

using nlohmann::json;
using namespace NextAI;

namespace
{
    std::filesystem::path SourceRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    }
}

TEST_CASE("AI service remains unavailable when the gnb sidecar is absent", "[Unit][AI][Bridge]")
{
#if defined(_WIN32)
    _putenv_s("GKNEXT_DISABLE_GNB_SIDECAR", "1");
#else
    setenv("GKNEXT_DISABLE_GNB_SIDECAR", "1", 1);
#endif
    {
        FAIService service;
        REQUIRE_FALSE(service.IsConfigured());
        REQUIRE(service.GetStatus() == EAIStatus::NotConfigured);
        REQUIRE_FALSE(service.SetProfile("magicalego-script"));
        REQUIRE_FALSE(service.SetCurrentModel("unused"));
        FChatRequest request;
        request.messages.push_back(FChatMessage::User("hello"));
        REQUIRE_FALSE(service.Chat(request).success);
        REQUIRE(service.GetStatus() == EAIStatus::NotConfigured);
    }
#if defined(_WIN32)
    _putenv_s("GKNEXT_DISABLE_GNB_SIDECAR", "");
#else
    unsetenv("GKNEXT_DISABLE_GNB_SIDECAR");
#endif
}

TEST_CASE("Gnb AI bridge v2 shared fixtures contain valid JSON-RPC frames", "[Unit][AI][Bridge]")
{
    const auto fixtureDir = SourceRoot() / "tools" / "tests" / "fixtures" / "gnb-ai-protocol" / "v2";
    REQUIRE(std::filesystem::is_directory(fixtureDir));
    size_t fixtureCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(fixtureDir))
    {
        if (entry.path().extension() != ".ndjson") continue;
        fixtureCount++;
        std::ifstream stream(entry.path());
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty()) continue;
            const json frame = json::parse(line);
            std::string error;
            REQUIRE(FGnbAIClient::ValidateProtocolFrame(frame, error));
        }
    }
    REQUIRE(fixtureCount >= 5);
}

TEST_CASE("GnbAIClient starts the real bridge and performs handshake", "[Unit][AI][Bridge]")
{
#if ANDROID || IOS
    SKIP("desktop sidecar only");
#else
    FGnbAIClient client;
    std::string error;
    REQUIRE(client.StartDefault(SourceRoot(), error));
    const json providers = client.Request("providers.list").get();
    REQUIRE(providers.is_array());
    REQUIRE(std::any_of(providers.begin(), providers.end(), [](const json& item) {
        return item.value("id", "") == "localllm";
    }));
    client.Stop();
    REQUIRE_FALSE(client.IsAvailable());
#endif
}
