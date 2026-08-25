#include "LauncherGameInstance.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"

#include <imgui.h>

#include <algorithm>

using Modules::NextDotNet::EGameSessionState;
using Modules::NextDotNet::FManagedGameManifest;

namespace
{
    /// Every native module this executable links, in the form manifests declare them. The launcher
    /// is the union of what its games need; anything outside this list cannot be acquired at
    /// runtime, which is why a manifest asking for it is refused rather than half-loaded.
    const std::vector<std::string> kLinkedModules = {
        "DevTools",      "GltfLoader",  "LDrawLoader",   "LiveCoding",     "NextAudio",
        "NextCapture",   "NextDotNet",  "NextFidelityFX", "NextPhysics",   "NextRemote",
        "NextStreamline", "NextTemporalUpscaler", "NextValidation", "NextUI",
        "ScadLoader",    "SceneContent", "SplatLoader",
    };

    const char* StateName(EGameSessionState state)
    {
        switch (state)
        {
        case EGameSessionState::Idle: return "Idle";
        case EGameSessionState::Loading: return "Loading";
        case EGameSessionState::Playing: return "Playing";
        case EGameSessionState::Unloading: return "Unloading";
        }
        return "Idle";
    }
}

LauncherGameInstance::LauncherGameInstance(Vulkan::WindowConfig& config,
                                           Runtime::Config::Options& options,
                                           NextEngine* engine)
    : ManagedGameHostInstance(config,
                              options,
                              engine,
                              Modules::NextDotNet::FManagedGameHostOptions{
                                  // No manifest: the launcher starts idle and loads what the player
                                  // picks. Everything else about hosting a managed game is inherited.
                                  .manifestPath = {},
                                  .window = {.title = "gkNextLauncher", .width = 1600, .height = 900, .forceSDR = true},
                                  .linkedModules = kLinkedModules,
                              })
{
}

void LauncherGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterString(
        "game.select", "", &selectedGameId_, NextCVar::ECVarFlags::None,
        "Managed game to run: a game id from assets/configs/games, or empty to return to the menu",
        [this]()
        {
            // Only records the request. Loading happens on the next tick, never inside a cvar
            // callback, which can arrive from anywhere including the session's own baseline
            // restore while a game is being torn down.
            pendingSelection_ = selectedGameId_;
            hasPendingSelection_ = true;
        });
}

void LauncherGameInstance::OnInit()
{
    ManagedGameHostInstance::OnInit();

    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetUserSettings().ShowOverlay = false;

    // game.select is this launcher's own control surface, not world state a game may disturb.
    // Restoring it on unload would write the previous game's id back and re-trigger the callback,
    // so stopping a game would immediately restart it.
    GetSession().SetBaselineExcludedCVars({"game.select"});

    RefreshEntries();

    // A game calling Engine.RequestClose() means "I am done", not "kill the process". The managed
    // side is unchanged: Brotato and Flappy quit exactly as they always did.
    GetSession().SetCloseRequestHandler(
        [this]() -> bool
        {
            GetSession().RequestUnload();
            return true;
        });

    // The menu needs a world to render into; without a committed scene the engine never reaches
    // Running and no UI is drawn at all.
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void LauncherGameInstance::RefreshEntries()
{
    entries_.clear();

    for (FManagedGameManifest& manifest :
         Modules::NextDotNet::ScanManagedGameManifests(Modules::NextDotNet::kManagedGameManifestDirectory))
    {
        FEntry entry;
        entry.manifest = std::move(manifest);

        if (std::string missing; !GetSession().AreRequirementsMet(entry.manifest, &missing))
        {
            entry.unavailableReason = "needs " + missing;
        }
        else
        {
            // The assembly is published beside the executable by CMake. A manifest can legitimately
            // outlive its game — a target excluded from this build, a partial publish — and saying
            // so beats an error the moment someone clicks it.
            const std::filesystem::path assemblyPath =
                NextRenderer::GetExecutableDirectory() / "csharp" / entry.manifest.assembly;
            std::error_code ec;
            if (!std::filesystem::exists(assemblyPath, ec))
            {
                entry.unavailableReason = "not built (" + entry.manifest.assembly + ")";
            }
        }

        entry.available = entry.unavailableReason.empty();
        entries_.push_back(std::move(entry));
    }

    highlightedIndex_ = 0;
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].available)
        {
            highlightedIndex_ = static_cast<int>(i);
            break;
        }
    }

    SPDLOG_INFO("[launcher] {} managed game(s) found", entries_.size());
}

void LauncherGameInstance::OnTick(double deltaSeconds)
{
    ManagedGameHostInstance::OnTick(deltaSeconds);
    ApplyPendingRebuild();
    ApplyPendingSelection();

    // Mirror the session back into the cvar so a console reader sees what is actually running.
    // Skipped while a request is in flight, which would otherwise clobber it before it is applied.
    if (!hasPendingSelection_)
    {
        const auto* active = GetSession().GetActiveManifest();
        selectedGameId_ = active != nullptr ? active->id : std::string();
    }
}

void LauncherGameInstance::ApplyPendingSelection()
{
    if (!hasPendingSelection_)
    {
        return;
    }
    // Transitions are queued as ticked tasks; asking for another one mid-flight would interleave
    // two loads. The request waits instead of being dropped.
    if (GetSession().GetState() == EGameSessionState::Loading ||
        GetSession().GetState() == EGameSessionState::Unloading)
    {
        return;
    }

    const std::string requested = pendingSelection_;
    hasPendingSelection_ = false;

    if (requested.empty())
    {
        GetSession().RequestUnload();
        return;
    }

    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&requested](const FEntry& entry) { return entry.manifest.id == requested; });
    if (it == entries_.end())
    {
        SPDLOG_ERROR("[launcher] no managed game with id '{}'", requested);
        return;
    }
    LoadEntry(static_cast<size_t>(std::distance(entries_.begin(), it)));
}

void LauncherGameInstance::ApplyPendingRebuild()
{
    if (pendingRebuildIndex_ < 0 || static_cast<size_t>(pendingRebuildIndex_) >= entries_.size())
    {
        pendingRebuildIndex_ = -1;
        return;
    }

    const FEntry& entry = entries_[static_cast<size_t>(pendingRebuildIndex_)];
    pendingRebuildIndex_ = -1;

    std::string error;
    if (GetSession().RebuildGame(entry.manifest, error))
    {
        rebuildStatus_ = "rebuilt " + entry.manifest.id;
        // A game that failed to load because it was never published becomes available now.
        RefreshEntries();
    }
    else
    {
        rebuildStatus_ = "rebuild failed: " + error;
        SPDLOG_ERROR("[launcher] {}", rebuildStatus_);
    }
}

void LauncherGameInstance::LoadEntry(size_t index)
{
    if (index >= entries_.size())
    {
        return;
    }
    const FEntry& entry = entries_[index];
    if (!entry.available)
    {
        SPDLOG_ERROR("[launcher] '{}' is unavailable: {}", entry.manifest.id, entry.unavailableReason);
        return;
    }
    GetSession().RequestLoad(entry.manifest);
}

bool LauncherGameInstance::OnGameRequestedClose()
{
    // Reached when a game quits without the session's handler being installed yet (a failure during
    // startup). Falling through to the default would close the process, which is never what the
    // launcher wants.
    GetSession().RequestUnload();
    return true;
}

bool LauncherGameInstance::OnHostKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return false;
    }

    if (GetSession().IsPlaying())
    {
        if (event.key.key == SDLK_ESCAPE)
        {
            GetSession().RequestUnload();
            return true;
        }
        // Everything else belongs to the running game.
        return false;
    }

    if (entries_.empty())
    {
        return false;
    }

    switch (event.key.key)
    {
    case SDLK_UP:
        highlightedIndex_ = (highlightedIndex_ + static_cast<int>(entries_.size()) - 1) %
                            static_cast<int>(entries_.size());
        return true;
    case SDLK_DOWN:
        highlightedIndex_ = (highlightedIndex_ + 1) % static_cast<int>(entries_.size());
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        LoadEntry(static_cast<size_t>(highlightedIndex_));
        return true;
    default:
        return false;
    }
}

bool LauncherGameInstance::OnHostRenderUI()
{
    if (GetSession().IsPlaying())
    {
        return false;
    }
    DrawMenu();
    return true;
}

void LauncherGameInstance::DrawMenu()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(640.0f, 480.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5f,
                                   viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("gkNext Launcher", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (GetSession().IsLeaking())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Previous games were not collected. Restart the launcher.");
        ImGui::Separator();
    }

    if (entries_.empty())
    {
        ImGui::TextWrapped("No games found under %s.", Modules::NextDotNet::kManagedGameManifestDirectory);
    }

    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const FEntry& entry = entries_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!entry.available);

        const bool highlighted = static_cast<int>(i) == highlightedIndex_;
        if (highlighted)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        }
        // The rebuild button is only reserved for when there is something to rebuild, so an
        // installed build (no C# sources, no project field) gets the full width for the game.
        const bool canRebuild = !entry.manifest.project.empty();
        const float playWidth = canRebuild ? -90.0f : -1.0f;
        if (ImGui::Button(entry.manifest.displayName.c_str(), ImVec2(playWidth, 40.0f)))
        {
            highlightedIndex_ = static_cast<int>(i);
            LoadEntry(i);
        }
        if (highlighted)
        {
            ImGui::PopStyleColor();
        }

        ImGui::EndDisabled();

        if (canRebuild)
        {
            ImGui::SameLine();
            // Not disabled with the rest of the entry: rebuilding is exactly what fixes a game
            // that is unavailable because it was never published.
            if (ImGui::Button("Rebuild", ImVec2(-1.0f, 40.0f)))
            {
                pendingRebuildIndex_ = static_cast<int>(i);
                rebuildStatus_ = "rebuilding " + entry.manifest.id + "...";
            }
        }

        if (!entry.available)
        {
            ImGui::TextDisabled("  unavailable: %s", entry.unavailableReason.c_str());
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Up/Down to choose, Enter to start, Esc in game to come back.");
    if (!rebuildStatus_.empty())
    {
        ImGui::TextDisabled("%s", rebuildStatus_.c_str());
    }
    if (!GetSession().GetLastError().empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", GetSession().GetLastError().c_str());
    }

    ImGui::End();
}

void LauncherGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("state", [this]() -> Runtime::Agent::FAgentQueryValue
            { return std::string(StateName(GetSession().GetState())); });
    reg.Add("active", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                const auto* manifest = GetSession().GetActiveManifest();
                return manifest != nullptr ? manifest->id : std::string();
            });
    reg.Add("count", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(entries_.size()); });
    reg.Add("availableCount", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                return static_cast<int64_t>(std::count_if(entries_.begin(), entries_.end(),
                                                          [](const FEntry& e) { return e.available; }));
            });
    reg.Add("highlighted", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                return highlightedIndex_ >= 0 && highlightedIndex_ < static_cast<int>(entries_.size())
                           ? entries_[static_cast<size_t>(highlightedIndex_)].manifest.id
                           : std::string();
            });
    reg.Add("lastError", [this]() -> Runtime::Agent::FAgentQueryValue { return GetSession().GetLastError(); });
    reg.Add("unloadPending", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(GetSession().UnloadPendingStreak()); });
    reg.Add("rebuildStatus", [this]() -> Runtime::Agent::FAgentQueryValue { return rebuildStatus_; });
}
