#include "Modules/NextDotNet/ManagedGameTemplate.hpp"

#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Modules::NextDotNet
{
    namespace
    {
        using json = nlohmann::json;
        /// Key order is insertion order, not alphabetical. A generated manifest is a file people
        /// read and edit next to the hand-written ones, and "assembly" before "id" reads as a
        /// machine artifact.
        using ordered_json = nlohmann::ordered_json;

        constexpr const char* kTemplateMetadataFile = "template.json";
        constexpr const char* kTemplateFilesDirectory = "files";
        constexpr const char* kTemplateAssetPath = "assets/templates/games";
        constexpr size_t kMaxNameLength = 48;

        /// The source assets directory (assets/), derived from where the C# sources are. Empty in a
        /// build that shipped without sources, which is exactly when nothing here may write.
        std::filesystem::path SourceAssetsRoot()
        {
            const std::filesystem::path managedSources = DotNetRuntime::ManagedSourceRoot();
            return managedSources.empty() ? std::filesystem::path{} : managedSources.parent_path();
        }

        bool ReadWholeFile(const std::filesystem::path& path, std::string& outData)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            outData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        bool WriteWholeFile(const std::filesystem::path& path, const std::string& data)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return false;
            }
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        /// Replaces every {{Token}} in `text`. Applied to file contents and to path components
        /// alike — a template file is named __ProjectName__.csproj, which is the same idea spelled
        /// so that it survives being a filename.
        std::string Substitute(std::string text, const std::vector<std::pair<std::string, std::string>>& tokens)
        {
            for (const auto& [token, value] : tokens)
            {
                const std::string braced = "{{" + token + "}}";
                const std::string underscored = "__" + token + "__";
                for (const std::string& pattern : {braced, underscored})
                {
                    size_t position = text.find(pattern);
                    while (position != std::string::npos)
                    {
                        text.replace(position, pattern.size(), value);
                        position = text.find(pattern, position + value.size());
                    }
                }
            }
            return text;
        }

        std::string Trimmed(std::string_view value)
        {
            const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            size_t begin = 0;
            size_t end = value.size();
            while (begin < end && isSpace(static_cast<unsigned char>(value[begin])))
            {
                ++begin;
            }
            while (end > begin && isSpace(static_cast<unsigned char>(value[end - 1])))
            {
                --end;
            }
            return std::string(value.substr(begin, end - begin));
        }

        bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(value[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i])))
                {
                    return false;
                }
            }
            return true;
        }

        /// Manifest ids already taken, from wherever a host would find them: the loose source tree
        /// and whatever the running process has mounted. Both matter — a build tree can hold a
        /// manifest the source tree does not, and reusing its id would make two games collide in
        /// the menu.
        std::vector<std::string> ExistingGameIds()
        {
            std::vector<std::string> ids;
            for (const FManagedGameManifest& manifest : ScanManagedGameManifests(kManagedGameManifestDirectory))
            {
                ids.push_back(manifest.id);
            }

            const std::filesystem::path sourceAssets = SourceAssetsRoot();
            if (!sourceAssets.empty())
            {
                std::error_code ec;
                const std::filesystem::path directory = sourceAssets / "configs" / "games";
                for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
                {
                    const std::string filename = entry.path().filename().string();
                    const std::string suffix = ".game.json";
                    if (filename.size() > suffix.size() &&
                        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0)
                    {
                        ids.push_back(filename.substr(0, filename.size() - suffix.size()));
                    }
                }
            }

            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        }

        void ReadOptionalTemplateBool(const json& source, const char* key, std::optional<bool>& target)
        {
            if (const auto it = source.find(key); it != source.end() && it->is_boolean())
            {
                target = it->get<bool>();
            }
        }

        std::optional<FGameTemplate> LoadTemplate(const std::filesystem::path& directory)
        {
            const std::filesystem::path metadataPath = directory / kTemplateMetadataFile;
            std::string data;
            if (!ReadWholeFile(metadataPath, data))
            {
                return std::nullopt;
            }

            FGameTemplate result;
            result.directory = directory;
            result.id = directory.filename().string();

            try
            {
                const json root = json::parse(data);

                if (const auto it = root.find("id"); it != root.end() && it->is_string())
                {
                    result.id = it->get<std::string>();
                }
                result.displayName = root.value("displayName", result.id);
                result.description = root.value("description", std::string());
                result.sortOrder = root.value("sortOrder", result.sortOrder);
                result.initialScene = root.value("initialScene", result.initialScene);
                result.hotReload = root.value("hotReload", result.hotReload);

                if (const auto it = root.find("highlights"); it != root.end() && it->is_array())
                {
                    for (const auto& entry : *it)
                    {
                        if (entry.is_string())
                        {
                            result.highlights.push_back(entry.get<std::string>());
                        }
                    }
                }
                if (const auto it = root.find("requiredModules"); it != root.end() && it->is_array())
                {
                    for (const auto& entry : *it)
                    {
                        if (entry.is_string())
                        {
                            result.requiredModules.push_back(entry.get<std::string>());
                        }
                    }
                }
                if (const auto window = root.find("window"); window != root.end() && window->is_object())
                {
                    result.window.title = window->value("title", std::string());
                    result.window.width = window->value("width", result.window.width);
                    result.window.height = window->value("height", result.window.height);
                    result.window.forceSDR = window->value("forceSDR", result.window.forceSDR);
                }
                if (const auto flags = root.find("showFlags"); flags != root.end() && flags->is_object())
                {
                    ReadOptionalTemplateBool(*flags, "debugGraphicsPanel", result.showFlags.debugGraphicsPanel);
                    ReadOptionalTemplateBool(*flags, "debugPhysicsOverlay", result.showFlags.debugPhysicsOverlay);
                    ReadOptionalTemplateBool(*flags, "overlay", result.showFlags.overlay);
                }
            }
            catch (const std::exception& error)
            {
                SPDLOG_ERROR("[template] {} is not valid JSON: {}", metadataPath.string(), error.what());
                return std::nullopt;
            }

            std::error_code ec;
            if (!std::filesystem::exists(directory / kTemplateFilesDirectory, ec))
            {
                SPDLOG_ERROR("[template] '{}' has no {}/ directory to copy", result.id, kTemplateFilesDirectory);
                return std::nullopt;
            }

            return result;
        }
    }

    std::filesystem::path GameTemplateRoot()
    {
        std::error_code ec;

        // The source tree first: creating a project writes into it anyway, and the copy under the
        // build output is only as fresh as the last build.
        if (const std::filesystem::path sourceAssets = SourceAssetsRoot(); !sourceAssets.empty())
        {
            const std::filesystem::path fromSource = sourceAssets / "templates" / "games";
            if (std::filesystem::exists(fromSource, ec))
            {
                return fromSource;
            }
        }

        const std::filesystem::path fromRuntime = Utilities::FileHelper::GetRuntimeFilePath(kTemplateAssetPath);
        if (std::filesystem::exists(fromRuntime, ec))
        {
            return fromRuntime;
        }
        return {};
    }

    std::vector<FGameTemplate> ScanGameTemplates()
    {
        std::vector<FGameTemplate> templates;

        const std::filesystem::path root = GameTemplateRoot();
        if (root.empty())
        {
            return templates;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            if (auto loaded = LoadTemplate(entry.path()))
            {
                templates.push_back(std::move(*loaded));
            }
        }

        std::sort(templates.begin(), templates.end(),
                  [](const FGameTemplate& lhs, const FGameTemplate& rhs)
                  {
                      return lhs.sortOrder != rhs.sortOrder ? lhs.sortOrder < rhs.sortOrder : lhs.id < rhs.id;
                  });
        return templates;
    }

    std::string DeriveGameId(std::string_view projectName)
    {
        std::string id;
        id.reserve(projectName.size());
        for (const char character : projectName)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (std::isalnum(raw) != 0)
            {
                id.push_back(static_cast<char>(std::tolower(raw)));
            }
        }
        // An id must start with a letter; a name like "3DGame" would otherwise produce one that
        // reads as a number in half the places it is used.
        if (!id.empty() && std::isdigit(static_cast<unsigned char>(id.front())) != 0)
        {
            id.insert(id.begin(), 'g');
        }
        return id;
    }

    bool ValidateNewGameRequest(const FNewGameRequest& request, std::string& outError)
    {
        const std::string projectName = Trimmed(request.projectName);
        const std::string gameId = Trimmed(request.gameId);
        const std::string displayName = Trimmed(request.displayName);

        if (request.templateId.empty())
        {
            outError = "pick a template";
            return false;
        }

        // The project name becomes a directory, an assembly name, a namespace and a class name at
        // once, so it has to satisfy the strictest of those: a C# identifier.
        if (projectName.empty())
        {
            outError = "project name is required";
            return false;
        }
        if (projectName.size() > kMaxNameLength)
        {
            outError = "project name is too long";
            return false;
        }
        if (std::isalpha(static_cast<unsigned char>(projectName.front())) == 0)
        {
            outError = "project name must start with a letter";
            return false;
        }
        for (const char character : projectName)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (std::isalnum(raw) == 0 && character != '_')
            {
                outError = "project name may only contain letters, digits and underscores";
                return false;
            }
        }
        if (StartsWithIgnoreCase(projectName, "GkNext"))
        {
            // GkNext.Engine, GkNext.Bootstrap and GkNext.Game are the shared managed assemblies.
            outError = "project name must not start with 'GkNext' — that prefix belongs to the engine";
            return false;
        }

        if (gameId.empty())
        {
            outError = "game id is required";
            return false;
        }
        if (gameId.size() > kMaxNameLength)
        {
            outError = "game id is too long";
            return false;
        }
        if (std::isalpha(static_cast<unsigned char>(gameId.front())) == 0 ||
            std::isupper(static_cast<unsigned char>(gameId.front())) != 0)
        {
            outError = "game id must start with a lowercase letter";
            return false;
        }
        for (const char character : gameId)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            const bool allowed = (std::isdigit(raw) != 0) ||
                                 (std::isalpha(raw) != 0 && std::isupper(raw) == 0) ||
                                 character == '-' || character == '_';
            if (!allowed)
            {
                outError = "game id may only contain lowercase letters, digits, '-' and '_'";
                return false;
            }
        }

        if (displayName.empty())
        {
            outError = "display name is required";
            return false;
        }

        const std::filesystem::path managedSources = DotNetRuntime::ManagedSourceRoot();
        if (managedSources.empty())
        {
            outError = "this build cannot reach the C# sources, so it cannot create a project";
            return false;
        }

        std::error_code ec;
        if (std::filesystem::exists(managedSources / projectName, ec))
        {
            outError = "assets/csharp/" + projectName + " already exists";
            return false;
        }

        const std::vector<std::string> takenIds = ExistingGameIds();
        if (std::find(takenIds.begin(), takenIds.end(), gameId) != takenIds.end())
        {
            outError = "a game with the id '" + gameId + "' already exists";
            return false;
        }

        outError.clear();
        return true;
    }

    FNewGameResult CreateManagedGame(const FGameTemplate& gameTemplate, const FNewGameRequest& request)
    {
        FNewGameResult result;

        FNewGameRequest normalized = request;
        normalized.projectName = Trimmed(request.projectName);
        normalized.gameId = Trimmed(request.gameId);
        normalized.displayName = Trimmed(request.displayName);

        // Re-validated here rather than trusted from the caller: this function writes files, and it
        // is reachable from a cvar and a test as well as from the dialog that already checked.
        if (!ValidateNewGameRequest(normalized, result.error))
        {
            return result;
        }

        const std::filesystem::path managedSources = DotNetRuntime::ManagedSourceRoot();
        const std::filesystem::path sourceAssets = SourceAssetsRoot();
        const std::filesystem::path projectDirectory = managedSources / normalized.projectName;
        const std::filesystem::path templateFiles = gameTemplate.directory / kTemplateFilesDirectory;

        std::error_code ec;
        if (!std::filesystem::exists(templateFiles, ec))
        {
            result.error = "template '" + gameTemplate.id + "' has no files to copy";
            return result;
        }

        const std::vector<std::pair<std::string, std::string>> tokens = {
            {"ProjectName", normalized.projectName},
            {"Namespace", normalized.projectName},
            {"AssemblyName", normalized.projectName},
            {"DisplayName", normalized.displayName},
            {"GameId", normalized.gameId},
            {"TemplateId", gameTemplate.id},
        };

        // From here on the project directory exists, so every failure has to take it back down
        // with it. A half-written project is worse than none: it blocks the retry.
        const auto fail = [&projectDirectory](std::string message) -> FNewGameResult
        {
            std::error_code removeError;
            std::filesystem::remove_all(projectDirectory, removeError);

            FNewGameResult failed;
            failed.error = std::move(message);
            return failed;
        };

        std::filesystem::create_directories(projectDirectory, ec);
        if (ec)
        {
            result.error = "could not create " + projectDirectory.string() + ": " + ec.message();
            return result;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(templateFiles, ec))
        {
            const std::filesystem::path relative = std::filesystem::relative(entry.path(), templateFiles, ec);
            if (ec)
            {
                return fail("could not walk the template directory: " + ec.message());
            }

            // Substituted per component so a token can name a directory as well as a file.
            std::filesystem::path destination = projectDirectory;
            for (const auto& component : relative)
            {
                destination /= Substitute(component.string(), tokens);
            }

            if (entry.is_directory())
            {
                std::filesystem::create_directories(destination, ec);
                if (ec)
                {
                    return fail("could not create " + destination.string() + ": " + ec.message());
                }
                continue;
            }

            std::string contents;
            if (!ReadWholeFile(entry.path(), contents))
            {
                return fail("could not read template file " + entry.path().string());
            }

            std::filesystem::create_directories(destination.parent_path(), ec);
            if (!WriteWholeFile(destination, Substitute(std::move(contents), tokens)))
            {
                return fail("could not write " + destination.string());
            }
            result.writtenFiles.push_back(destination);
        }

        // --- the manifest, which is what actually makes this a game ---------------------------
        FManagedGameManifest& manifest = result.manifest;
        manifest.id = normalized.gameId;
        manifest.displayName = normalized.displayName;
        // The publish subdirectory is the assembly path's parent, which is how a host derives where
        // to publish (ManagedGameSession::RebuildGame). Keep the two in step by construction.
        manifest.assembly = normalized.gameId + "/" + normalized.projectName + ".dll";
        manifest.project = normalized.projectName + "/" + normalized.projectName + ".csproj";
        manifest.window = gameTemplate.window;
        if (manifest.window.title.empty())
        {
            manifest.window.title = normalized.displayName;
        }
        manifest.requiredModules = gameTemplate.requiredModules;
        manifest.initialScene = gameTemplate.initialScene;
        manifest.showFlags = gameTemplate.showFlags;
        manifest.hotReload = gameTemplate.hotReload;
        manifest.compileManagedSources = false;

        ordered_json manifestJson;
        manifestJson["id"] = manifest.id;
        manifestJson["displayName"] = manifest.displayName;
        manifestJson["assembly"] = manifest.assembly;
        manifestJson["project"] = manifest.project;
        manifestJson["window"] = {
            {"title", manifest.window.title},
            {"width", manifest.window.width},
            {"height", manifest.window.height},
            {"forceSDR", manifest.window.forceSDR},
        };
        manifestJson["requiredModules"] = manifest.requiredModules;
        manifestJson["initialScene"] = manifest.initialScene;
        ordered_json showFlagsJson = ordered_json::object();
        if (manifest.showFlags.debugGraphicsPanel.has_value())
        {
            showFlagsJson["debugGraphicsPanel"] = *manifest.showFlags.debugGraphicsPanel;
        }
        if (manifest.showFlags.debugPhysicsOverlay.has_value())
        {
            showFlagsJson["debugPhysicsOverlay"] = *manifest.showFlags.debugPhysicsOverlay;
        }
        if (manifest.showFlags.overlay.has_value())
        {
            showFlagsJson["overlay"] = *manifest.showFlags.overlay;
        }
        manifestJson["showFlags"] = showFlagsJson;
        manifestJson["hotReload"] = manifest.hotReload;
        manifestJson["compileManagedSources"] = manifest.compileManagedSources;

        const std::string manifestText = manifestJson.dump(2) + "\n";
        const std::string manifestFilename = manifest.id + ".game.json";
        const std::filesystem::path manifestSourcePath =
            sourceAssets / "configs" / "games" / manifestFilename;

        std::filesystem::create_directories(manifestSourcePath.parent_path(), ec);
        if (!WriteWholeFile(manifestSourcePath, manifestText))
        {
            return fail("could not write " + manifestSourcePath.string());
        }
        result.writtenFiles.push_back(manifestSourcePath);
        manifest.sourcePath = manifestSourcePath.string();

        // The build tree holds a *copy* of assets, and that copy is what a running host scans. Only
        // writing the source tree would mean the new game appeared after the next build rather than
        // straight away; only writing the runtime copy would lose it at the next configure.
        const std::filesystem::path manifestRuntimePath = Utilities::FileHelper::GetRuntimeFilePath(
            std::string(kManagedGameManifestDirectory) + "/" + manifestFilename);
        if (manifestRuntimePath != manifestSourcePath &&
            std::filesystem::exists(manifestRuntimePath.parent_path(), ec))
        {
            if (WriteWholeFile(manifestRuntimePath, manifestText))
            {
                result.writtenFiles.push_back(manifestRuntimePath);
            }
            else
            {
                SPDLOG_WARN("[template] could not mirror the manifest to {}; the new game will only "
                            "appear after the next build",
                            manifestRuntimePath.string());
            }
        }

        result.created = true;
        result.projectDirectory = projectDirectory;
        result.projectFile = projectDirectory / (normalized.projectName + ".csproj");
        result.manifestFile = manifestSourcePath;
        SPDLOG_INFO("[template] created '{}' from '{}' at {}", manifest.id, gameTemplate.id,
                    projectDirectory.string());
        return result;
    }
}
