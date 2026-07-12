#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentClient.hpp"

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

TEST_CASE("Gnb bridge v1 shared fixtures contain valid JSON-RPC frames", "[Unit][AI][Bridge]")
{
    const auto fixtureDir = SourceRoot() / "tests" / "fixtures" / "gnb-agent-protocol" / "v1";
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
            REQUIRE(FGnbAgentClient::ValidateProtocolFrame(frame, error));
        }
    }
    REQUIRE(fixtureCount >= 5);
}

TEST_CASE("GnbAgentClient starts the real bridge and performs handshake", "[Unit][AI][Bridge]")
{
#if ANDROID || IOS
    SKIP("desktop sidecar only");
#else
    FGnbAgentClient client;
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
