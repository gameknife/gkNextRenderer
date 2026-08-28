#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextDotNet/ManagedGameManifest.hpp"

namespace Modules::NextDotNet
{
    /// One project template, read from assets/templates/games/<id>/template.json.
    ///
    /// A template is content, not code: adding one is a directory with a metadata file and a tree
    /// of files to copy. Nothing in the engine, the launcher or the editor enumerates template ids,
    /// so a new template appears in every host that scans the directory without a rebuild.
    struct FGameTemplate
    {
        std::string id;
        std::string displayName;
        /// A paragraph for the dialog: what the generated game already does.
        std::string description;
        /// Bullet points under the description. Short — these sit in a fixed-width panel.
        std::vector<std::string> highlights;
        /// Menu order. Ties break on id, so a template with no sortOrder still lands somewhere
        /// stable rather than wherever the filesystem returned it.
        int sortOrder = 100;

        /// Defaults for the manifest written next to the generated project. Everything here is
        /// what the *template* wants; the user only ever supplies names.
        std::vector<std::string> requiredModules;
        std::string initialScene = "Empty.proc";
        FManagedGameManifest::FWindow window;
        FManagedGameManifest::FShowFlagOverrides showFlags;
        bool hotReload = true;

        /// Directory holding template.json and the files/ tree. Absolute.
        std::filesystem::path directory;
    };

    /// Where templates are read from, or empty when this build cannot reach any. Prefers the source
    /// tree over the copy in the build output: creating a project only works in a source tree
    /// anyway, and a stale copy would generate from yesterday's template.
    std::filesystem::path GameTemplateRoot();

    /// Every readable template, sorted by (sortOrder, id). A malformed template is logged and
    /// skipped — one bad directory must not hide the rest.
    std::vector<FGameTemplate> ScanGameTemplates();

    /// What the user supplies. Everything else about the new game comes from the template.
    struct FNewGameRequest
    {
        std::string templateId;
        /// PascalCase C# identifier. Names the directory, the assembly, the namespace and the
        /// generated class, which is why it is validated as strictly as an identifier.
        std::string projectName;
        std::string displayName;
        /// Manifest id, also the publish subdirectory. Lowercase.
        std::string gameId;
    };

    /// The id a project name suggests: "MySpaceGame" -> "myspacegame". Only ever a default — the
    /// dialog lets it be overridden, because an id is also a filename people have to type.
    std::string DeriveGameId(std::string_view projectName);

    /// Everything that must hold before a single file is written: name shapes, reserved names, and
    /// collisions with an existing project, directory or manifest id.
    ///
    /// outError is written for the dialog to show verbatim, so it names the field that is wrong.
    /// Returns false on the first problem rather than collecting them: fixing one often changes
    /// the others, and a list of five errors on a four-field form is noise.
    bool ValidateNewGameRequest(const FNewGameRequest& request, std::string& outError);

    struct FNewGameResult
    {
        bool created = false;
        /// Set when created is false. Already user-facing.
        std::string error;

        /// The manifest as written. Hand this to ManagedGameSession::RebuildGame to publish the
        /// new project, and to RequestLoad to run it.
        FManagedGameManifest manifest;

        std::filesystem::path projectDirectory;
        std::filesystem::path projectFile;
        std::filesystem::path manifestFile;
        /// Every file written, project tree and manifest alike. Diagnostics and tests.
        std::vector<std::filesystem::path> writtenFiles;
    };

    /// Writes a new managed game: the C# project under assets/csharp/<ProjectName>/ and its
    /// manifest under assets/configs/games/. Does not build it — publishing takes seconds and the
    /// caller is expected to have drawn a frame saying so. ManagedGameSession::RebuildGame is the
    /// build, and it works on the returned manifest with no extra bookkeeping.
    ///
    /// Atomic in the way that matters: a failure part-way through removes the project directory it
    /// created, so a retry is not blocked by the wreckage of the previous attempt.
    FNewGameResult CreateManagedGame(const FGameTemplate& gameTemplate, const FNewGameRequest& request);
}
