#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <catch2/catch_test_macros.hpp>

namespace
{
    std::filesystem::path WriteTestPak(const std::string& entry, const std::vector<uint8_t>& data)
    {
        const std::filesystem::path pakPath = std::filesystem::temp_directory_path() /
            ("gknext_file_helper_" + Utilities::NameHelper::RandomName(12) + ".pak");
        std::ofstream writer(pakPath, std::ios::binary);
        REQUIRE(writer.is_open());

        const uint32_t entryCount = 1;
        const uint32_t offset = static_cast<uint32_t>(3 + sizeof(uint32_t) + entry.size() + 1 + 3 * sizeof(uint32_t));
        const uint32_t size = static_cast<uint32_t>(data.size());
        writer.write("GNP", 3);
        writer.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));
        writer.write(entry.c_str(), static_cast<std::streamsize>(entry.size() + 1));
        writer.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
        writer.write(reinterpret_cast<const char*>(&size), sizeof(size));
        writer.write(reinterpret_cast<const char*>(&size), sizeof(size));
        writer.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        writer.close();
        return pakPath;
    }

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

TEST_CASE("Pak memory reads and directory probes do not materialize assets", "[Unit][FileHelper]")
{
    const std::string entry = "assets/tests/file_helper_probe.bin";
    const std::filesystem::path pakPath = WriteTestPak(entry, {1, 2, 3, 4});
    const std::filesystem::path cachePath =
        (Utilities::FileHelper::GetWritableRuntimeRoot() / "asset-cache" / entry).lexically_normal();
    std::error_code errorCode;
    std::filesystem::remove(cachePath, errorCode);

    {
        Utilities::Package::FPackageFileSystem package(Utilities::Package::EPM_OsFile);
        package.MountPak(pakPath.string());

        std::vector<uint8_t> data;
        REQUIRE(package.LoadFile(entry, data));
        CHECK(data == std::vector<uint8_t>{1, 2, 3, 4});
        CHECK_FALSE(std::filesystem::exists(cachePath));

        const std::filesystem::path directoryPath = Utilities::FileHelper::GetPlatformFilePath(
            "assets/tests/file_helper_probe");
        CHECK(directoryPath == Utilities::FileHelper::GetRuntimeFilePath("assets/tests/file_helper_probe"));
        CHECK_FALSE(std::filesystem::exists(cachePath));
    }

    std::filesystem::remove(pakPath, errorCode);
    std::filesystem::remove(cachePath, errorCode);
}

TEST_CASE("Writable paths stay inside the application data root", "[Unit][FileHelper]")
{
    const std::filesystem::path writableRoot = Utilities::FileHelper::GetWritableRuntimeRoot();

    CHECK(Utilities::FileHelper::ResolveWritablePath("settings/../layout.ini") ==
          (writableRoot / "layout.ini").lexically_normal());
    CHECK(Utilities::FileHelper::ResolveWritablePath("../../outside.ini") ==
          (writableRoot / "outside.ini").lexically_normal());

    const std::filesystem::path explicitPath =
        (std::filesystem::temp_directory_path() / "gknext_explicit_output.ini").lexically_normal();
    CHECK(Utilities::FileHelper::ResolveWritablePath(explicitPath) == explicitPath);
}
