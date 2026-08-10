#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <catch2/catch_test_macros.hpp>

namespace
{
    class FAssetTraceScope
    {
    public:
        explicit FAssetTraceScope(std::filesystem::path path): path_(std::move(path))
        {
            std::error_code errorCode;
            std::filesystem::remove(path_, errorCode);
            Utilities::FileHelper::SetAssetTracePath(path_);
        }

        ~FAssetTraceScope()
        {
            Utilities::FileHelper::SetAssetTracePath({});
            std::error_code errorCode;
            std::filesystem::remove(path_, errorCode);
        }

        const std::filesystem::path& GetPath() const { return path_; }

    private:
        std::filesystem::path path_;
    };
}

TEST_CASE("Asset trace records concrete reads but not discovery", "[Unit][FileHelper]")
{
    const std::filesystem::path tracePath = std::filesystem::temp_directory_path() /
        ("gknext_asset_trace_" + Utilities::NameHelper::RandomName(12) + ".txt");
    FAssetTraceScope traceScope(tracePath);

    // Scene-list directory discovery and availability probes must not turn every
    // selectable scene into a package dependency.
    Utilities::FileHelper::GetPlatformFilePath("assets/models");
    REQUIRE_FALSE(std::filesystem::exists(traceScope.GetPath()));
    REQUIRE(Utilities::FileHelper::IsAssetAvailable("assets/models/playground.glb"));
    REQUIRE_FALSE(std::filesystem::exists(traceScope.GetPath()));

    // Resolving a concrete file for a downstream file API is a runtime dependency.
    Utilities::FileHelper::GetPlatformFilePath("assets/models/playground.glb");
    std::ifstream traceReader(traceScope.GetPath());
    REQUIRE(traceReader.is_open());
    std::string tracedAsset;
    std::getline(traceReader, tracedAsset);
    REQUIRE(tracedAsset == "assets/models/playground.glb");
}
