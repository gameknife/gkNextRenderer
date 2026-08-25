#include "Modules/NextDotNet/ManagedGameManifest.hpp"

#include "Engine/Utilities/FileHelper.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>

namespace Modules::NextDotNet
{
    namespace
    {
        using json = nlohmann::json;

        constexpr const char* kManifestExtension = ".game.json";

        /// Reads an asset-relative or absolute path through the pak system when one is mounted,
        /// falling back to a loose file. Mirrors CVarSystem's config reader: manifests are shipped
        /// content and must survive being paked.
        bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& outData)
        {
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                if (package->LoadFile(path, outData))
                {
                    return true;
                }
            }

            const std::filesystem::path loosePath = Utilities::FileHelper::GetRuntimeFilePath(path);
            std::ifstream file(loosePath, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            outData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        void ReadOptionalBool(const json& source, const char* key, std::optional<bool>& target)
        {
            if (const auto it = source.find(key); it != source.end() && it->is_boolean())
            {
                target = it->get<bool>();
            }
        }

        std::string StemOf(const std::string& path)
        {
            const std::string filename = std::filesystem::path(path).filename().string();
            const std::string suffix = kManifestExtension;
            if (filename.size() > suffix.size() &&
                filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                return filename.substr(0, filename.size() - suffix.size());
            }
            return std::filesystem::path(filename).stem().string();
        }
    }

    std::optional<FManagedGameManifest> LoadManagedGameManifest(const std::string& path)
    {
        std::vector<uint8_t> data;
        if (!ReadFileBytes(path, data))
        {
            SPDLOG_ERROR("[game] manifest not found: {}", path);
            return std::nullopt;
        }

        FManagedGameManifest manifest;
        manifest.sourcePath = path;
        manifest.id = StemOf(path);

        try
        {
            const json root = json::parse(data.begin(), data.end());

            if (const auto it = root.find("id"); it != root.end() && it->is_string())
            {
                manifest.id = it->get<std::string>();
            }
            manifest.displayName = root.value("displayName", manifest.id);
            manifest.assembly = root.value("assembly", std::string());
            manifest.project = root.value("project", std::string());
            manifest.initialScene = root.value("initialScene", std::string());
            manifest.hotReload = root.value("hotReload", true);
            manifest.compileManagedSources = root.value("compileManagedSources", false);

            if (const auto window = root.find("window"); window != root.end() && window->is_object())
            {
                manifest.window.title = window->value("title", manifest.displayName);
                manifest.window.width = window->value("width", manifest.window.width);
                manifest.window.height = window->value("height", manifest.window.height);
                manifest.window.forceSDR = window->value("forceSDR", manifest.window.forceSDR);
            }
            if (manifest.window.title.empty())
            {
                manifest.window.title = manifest.displayName;
            }

            if (const auto modules = root.find("requiredModules"); modules != root.end() && modules->is_array())
            {
                for (const auto& entry : *modules)
                {
                    if (entry.is_string())
                    {
                        manifest.requiredModules.push_back(entry.get<std::string>());
                    }
                }
            }

            if (const auto flags = root.find("showFlags"); flags != root.end() && flags->is_object())
            {
                ReadOptionalBool(*flags, "debugGraphicsPanel", manifest.showFlags.debugGraphicsPanel);
                ReadOptionalBool(*flags, "debugPhysicsOverlay", manifest.showFlags.debugPhysicsOverlay);
                ReadOptionalBool(*flags, "overlay", manifest.showFlags.overlay);
            }
        }
        catch (const std::exception& error)
        {
            SPDLOG_ERROR("[game] manifest {} is not valid JSON: {}", path, error.what());
            return std::nullopt;
        }

        // The assembly is the one field with no sensible default: without it there is no game.
        if (manifest.assembly.empty())
        {
            SPDLOG_ERROR("[game] manifest {} has no 'assembly' field", path);
            return std::nullopt;
        }

        return manifest;
    }

    std::vector<FManagedGameManifest> ScanManagedGameManifests(const std::string& directory)
    {
        // A paked build and a loose tree can both be present; collect names first so a manifest
        // shipped in a pak and also sitting on disk is only loaded once.
        std::set<std::string> relativePaths;

        std::error_code ec;
        const std::filesystem::path looseDirectory = Utilities::FileHelper::GetRuntimeFilePath(directory);
        if (std::filesystem::exists(looseDirectory, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(looseDirectory, ec))
            {
                if (ec)
                {
                    break;
                }
                const std::string filename = entry.path().filename().string();
                if (filename.size() > std::strlen(kManifestExtension) &&
                    filename.find(kManifestExtension) == filename.size() - std::strlen(kManifestExtension))
                {
                    relativePaths.insert(directory + "/" + filename);
                }
            }
        }

        if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
        {
            for (const std::string& entry : package->ListMountedEntries(directory))
            {
                if (entry.find(kManifestExtension) != std::string::npos)
                {
                    relativePaths.insert(entry);
                }
            }
        }

        std::vector<FManagedGameManifest> manifests;
        manifests.reserve(relativePaths.size());
        for (const std::string& path : relativePaths)
        {
            if (auto manifest = LoadManagedGameManifest(path))
            {
                manifests.push_back(std::move(*manifest));
            }
        }

        std::sort(manifests.begin(), manifests.end(),
                  [](const FManagedGameManifest& lhs, const FManagedGameManifest& rhs)
                  {
                      return lhs.id < rhs.id;
                  });
        return manifests;
    }
}
