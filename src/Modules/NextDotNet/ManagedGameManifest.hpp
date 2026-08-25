#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <optional>

namespace Modules::NextDotNet
{
    /// Everything that used to differ between one C# game's native shell and another's, expressed
    /// as data. See docs/designs/managed-game-launcher-design.md section 4.2.
    ///
    /// One manifest is the single declaration of a managed game: the per-game executable reads it
    /// to configure its window and load its assembly, and gkNextLauncher reads the same file to
    /// populate its menu. Two sources of truth here would mean a game that behaves differently
    /// depending on how it was started.
    struct FManagedGameManifest
    {
        struct FWindow
        {
            std::string title;
            int width = 1280;
            int height = 720;
            bool forceSDR = true;
        };

        /// Only the flags a manifest actually mentions are applied; the rest keep engine defaults.
        /// A plain bool would silently force every unmentioned flag to false.
        struct FShowFlagOverrides
        {
            std::optional<bool> debugGraphicsPanel;
            std::optional<bool> debugPhysicsOverlay;
            std::optional<bool> overlay;
        };

        /// Stable identifier, also the menu sort key. Defaults to the file stem.
        std::string id;
        std::string displayName;

        /// Managed assembly, relative to <bin>/csharp — the same path shape FConfig::gameAssembly
        /// takes, and the same subdirectory gk_dotnet_managed_game(... DIR ...) publishes into.
        std::string assembly;

        /// C# project, relative to assets/csharp. Optional, and only meaningful in a source tree:
        /// it lets a host rebuild the game without leaving it, which is the whole point of running
        /// managed code under CoreCLR. An installed build simply has no project to point at.
        std::string project;

        FWindow window;

        /// Native modules the game needs at runtime. Purely declarative: modules are static
        /// libraries chosen at link time, so a host cannot acquire a missing one. The launcher
        /// checks this against what it was built with and refuses the game up front rather than
        /// letting it fail halfway into a scene it cannot load.
        std::vector<std::string> requiredModules;

        /// Scene to request once the game is initialised. Empty means the game loads its own from
        /// managed code (what Brotato3D does through Engine.RequestLoadScene).
        std::string initialScene;

        FShowFlagOverrides showFlags;

        bool hotReload = true;
        bool compileManagedSources = false;

        /// Where this manifest was read from. Only used in diagnostics.
        std::string sourcePath;
    };

    /// Reads one manifest. Returns nullopt and logs the reason on a missing file, malformed JSON,
    /// or a missing required field.
    std::optional<FManagedGameManifest> LoadManagedGameManifest(const std::string& path);

    /// Every *.game.json under a directory, loose files and mounted pak entries alike, sorted by
    /// id. Unreadable manifests are logged and skipped: one bad file must not hide the rest.
    std::vector<FManagedGameManifest> ScanManagedGameManifests(const std::string& directory);

    /// Default manifest location, relative to the runtime root.
    inline constexpr const char* kManagedGameManifestDirectory = "assets/configs/games";
}
