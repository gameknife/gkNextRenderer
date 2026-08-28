#include "Modules/NextDotNet/ManagedGameTemplate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

// Covers the half of "new game project" that writes files. The dialog around it is ImGui and is
// checked by eye; what must not regress is that a bad name is refused *before* a directory appears,
// and that a good one produces a project whose tokens are actually substituted.

using namespace Modules::NextDotNet;

namespace
{
    /// Points ManagedSourceRoot() at a scratch tree, so a test that creates a project cannot write
    /// into the repository's own assets/csharp. Empty removes the override.
    void SetManagedSourcesOverride(const std::string& value)
    {
#if defined(_WIN32)
        _putenv_s("GK_DOTNET_MANAGED_SOURCES", value.c_str());
#else
        if (value.empty())
        {
            unsetenv("GK_DOTNET_MANAGED_SOURCES");
        }
        else
        {
            setenv("GK_DOTNET_MANAGED_SOURCES", value.c_str(), 1);
        }
#endif
    }

    std::string ReadWholeFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
}

TEST_CASE("DeriveGameId turns a project name into a manifest id", "[Unit][DotNet][Template]")
{
    CHECK(DeriveGameId("MySpaceGame") == "myspacegame");
    CHECK(DeriveGameId("Space_Game 2") == "spacegame2");
    // An id that starts with a digit reads as a number wherever it is used, so it gets a prefix.
    CHECK(DeriveGameId("3DShooter") == "g3dshooter");
    CHECK(DeriveGameId("") == "");
}

TEST_CASE("the shipped game templates are readable", "[Unit][DotNet][Template]")
{
    const std::vector<FGameTemplate> templates = ScanGameTemplates();
    REQUIRE_FALSE(templates.empty());

    for (const FGameTemplate& gameTemplate : templates)
    {
        INFO("template " << gameTemplate.id);
        CHECK_FALSE(gameTemplate.id.empty());
        CHECK_FALSE(gameTemplate.displayName.empty());
        CHECK_FALSE(gameTemplate.description.empty());
        // Every template must produce a buildable project, which means a csproj and a game class.
        CHECK(std::filesystem::exists(gameTemplate.directory / "files" / "__ProjectName__.csproj"));
        CHECK(std::filesystem::exists(gameTemplate.directory / "files" / "__ProjectName__Game.cs"));
    }

    // Sorted by (sortOrder, id), so a menu built from this is stable across runs.
    for (size_t i = 1; i < templates.size(); ++i)
    {
        const bool ordered = templates[i - 1].sortOrder < templates[i].sortOrder ||
                             (templates[i - 1].sortOrder == templates[i].sortOrder &&
                              templates[i - 1].id < templates[i].id);
        CHECK(ordered);
    }
}

TEST_CASE("a new game request is validated before anything is written", "[Unit][DotNet][Template]")
{
    const auto request = [](std::string projectName, std::string gameId)
    {
        FNewGameRequest value;
        value.templateId = "blank";
        value.projectName = std::move(projectName);
        value.displayName = "Test Game";
        value.gameId = std::move(gameId);
        return value;
    };

    std::string error;
    CHECK(ValidateNewGameRequest(request("MyTestGame", "mytestgame"), error));

    CHECK_FALSE(ValidateNewGameRequest(request("", "mytestgame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("9Lives", "ninelives"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("My Game", "mygame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("My.Game", "mygame"), error));
    // The GkNext prefix belongs to the shared managed assemblies.
    CHECK_FALSE(ValidateNewGameRequest(request("GkNextThing", "gknextthing"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "MyTestGame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "my game"), error));
    // An id that already exists: 'sandbox' is committed under assets/configs/games.
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "sandbox"), error));

    FNewGameRequest noTemplate = request("MyTestGame", "mytestgame");
    noTemplate.templateId.clear();
    CHECK_FALSE(ValidateNewGameRequest(noTemplate, error));

    // Every rejection has to say something the dialog can show; an empty message is a silent fail.
    CHECK_FALSE(error.empty());
}

TEST_CASE("creating a game writes a substituted project and a manifest", "[Unit][DotNet][Template]")
{
    // Scanned before the override, so the templates come from the repository rather than the
    // scratch tree the project is written into.
    const std::vector<FGameTemplate> templates = ScanGameTemplates();
    REQUIRE_FALSE(templates.empty());

    const auto blank = std::find_if(templates.begin(), templates.end(),
                                    [](const FGameTemplate& t) { return t.id == "blank"; });
    REQUIRE(blank != templates.end());

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "gkNextTemplateTest" / "assets" / "csharp";
    std::error_code ec;
    std::filesystem::remove_all(scratch.parent_path().parent_path(), ec);
    std::filesystem::create_directories(scratch, ec);
    REQUIRE_FALSE(ec);

    SetManagedSourcesOverride(scratch.string());

    FNewGameRequest request;
    request.templateId = blank->id;
    request.projectName = "TemplateProbeGame";
    request.displayName = "Template Probe";
    request.gameId = "templateprobegame";

    const FNewGameResult result = CreateManagedGame(*blank, request);
    INFO(result.error);
    REQUIRE(result.created);

    CHECK(std::filesystem::exists(result.projectFile));
    CHECK(result.manifest.assembly == "templateprobegame/TemplateProbeGame.dll");
    CHECK(result.manifest.project == "TemplateProbeGame/TemplateProbeGame.csproj");
    CHECK(std::filesystem::exists(result.manifestFile));

    // The publish subdirectory a host derives from the assembly path has to be the manifest id, or
    // a rebuild publishes somewhere the next load will not look.
    CHECK(std::filesystem::path(result.manifest.assembly).parent_path().string() == result.manifest.id);

    const std::string gameSource =
        ReadWholeFile(result.projectDirectory / "TemplateProbeGameGame.cs");
    CHECK_FALSE(gameSource.empty());
    CHECK(gameSource.find("{{") == std::string::npos);
    CHECK(gameSource.find("__ProjectName__") == std::string::npos);
    CHECK(gameSource.find("namespace TemplateProbeGame;") != std::string::npos);
    CHECK(gameSource.find("class TemplateProbeGameGame") != std::string::npos);

    const std::string projectFile = ReadWholeFile(result.projectFile);
    CHECK(projectFile.find("<AssemblyName>TemplateProbeGame</AssemblyName>") != std::string::npos);

    // A second attempt with the same names must be refused rather than half-overwriting the first.
    std::string error;
    CHECK_FALSE(ValidateNewGameRequest(request, error));

    for (const std::filesystem::path& written : result.writtenFiles)
    {
        std::filesystem::remove(written, ec);
    }
    std::filesystem::remove_all(scratch.parent_path().parent_path(), ec);
    SetManagedSourcesOverride({});
}
