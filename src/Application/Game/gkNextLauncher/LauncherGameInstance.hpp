#pragma once

#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

/// One process that runs any managed game, instead of one executable per game.
///
/// Only meaningful under CoreCLR: NativeAOT links exactly one game into the binary, so a launcher
/// there would have nothing to choose between (see docs/designs/managed-game-launcher-design.md
/// section 1). CMake keeps this target out of an AOT configuration entirely.
class LauncherGameInstance final : public Modules::NextDotNet::ManagedGameHostInstance
{
public:
    LauncherGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~LauncherGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    bool OnGameRequestedClose() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

protected:
    bool OnHostRenderUI() override;
    bool OnHostKey(SDL_Event& event) override;

private:
    /// A manifest plus whether this host can actually run it. Availability is resolved once at
    /// startup so the menu can explain a greyed-out entry instead of failing on click.
    struct FEntry
    {
        Modules::NextDotNet::FManagedGameManifest manifest;
        bool available = false;
        std::string unavailableReason;
    };

    void RefreshEntries();
    void ApplyPendingSelection();
    void ApplyPendingRebuild();
    void LoadEntry(size_t index);

    void DrawMenu();

    std::vector<FEntry> entries_;
    int highlightedIndex_ = 0;

    /// Control channel for scripted validation and the console: set it to a game id to run that
    /// game, or to an empty string to return to the menu. Never read as the source of truth —
    /// ApplyPendingSelection consumes it and then mirrors the session's actual state back.
    std::string selectedGameId_;
    std::string pendingSelection_;
    bool hasPendingSelection_ = false;

    /// A publish blocks for seconds. The click only records the request so the menu can draw one
    /// frame saying what it is doing before the process stops responding.
    int pendingRebuildIndex_ = -1;
    std::string rebuildStatus_;
};
