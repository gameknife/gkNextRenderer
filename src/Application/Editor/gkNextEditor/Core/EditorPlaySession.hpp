#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;

namespace Editor
{
    enum class EPlayState
    {
        /// Normal editing. No managed game is loaded.
        Stopped,
        /// A game is running and owns input and the render camera.
        Playing,
        /// The game is still running — its scene is live and inspectable — but input and the camera
        /// belong to the editor again. This is what makes a Play session editable rather than
        /// something you can only watch.
        Ejected,
    };

    struct FPlayGameEntry
    {
        std::string id;
        std::string displayName;
        bool available = false;
        std::string unavailableReason;
        bool canRebuild = false;
    };

    /// Play-in-editor: runs a C# game inside the editor process, through the same
    /// ManagedGameSession the launcher uses.
    ///
    /// Deliberately narrow. Stopping does **not** restore the editor state the session started
    /// with — it reloads the scene that was open before Play, from disk, and everything else
    /// (selection, undo history, camera) starts fresh. Anything authored during a Play session is
    /// therefore lost. That is a real limitation, not an oversight: preserving edits across a Play
    /// session needs a world snapshot, which is a much larger problem than running the game, and
    /// running the game is what this is for. See docs/designs/managed-game-launcher-design.md §7.
    ///
    /// The header deliberately names no NextDotNet type, so gkNextEditor still builds where .NET
    /// is unavailable; every entry point then degrades to "unavailable" instead of vanishing.
    class FPlaySession final
    {
    public:
        explicit FPlaySession(NextEngine& engine);
        ~FPlaySession();

        FPlaySession(const FPlaySession&) = delete;
        FPlaySession& operator=(const FPlaySession&) = delete;

        /// Scans the game manifests and installs the quit handler. Call once from OnInit.
        void Initialize();

        /// False when this build has no managed runtime, or has one that cannot choose a game at
        /// runtime (NativeAOT links exactly one game into the binary). Reason() says which.
        bool IsAvailable() const;
        const std::string& UnavailableReason() const;

        const std::vector<FPlayGameEntry>& Games() const;
        EPlayState State() const;
        bool IsRunning() const { return State() != EPlayState::Stopped; }
        /// Only true while the game owns input; false when stopped or ejected. The editor uses this
        /// to decide whether its own camera and shortcuts should react at all.
        bool GameOwnsInput() const { return State() == EPlayState::Playing; }
        std::string ActiveGameId() const;
        std::string LastError() const;

        /// Starts a game. `returnScene` is reloaded when the session stops; pass the scene the
        /// editor currently has open.
        void Play(const std::string& gameId, std::string returnScene);
        void Stop();
        void SetEjected(bool ejected);
        void ToggleEject();

        bool IsPaused() const;
        void SetPaused(bool paused);
        void TogglePause();

        /// Republishes a game's C# from source. Only meaningful while stopped: a rebuild lands on
        /// disk, and the next Play picks it up.
        bool Rebuild(const std::string& gameId, std::string& outError);

        // --- new project from a template -------------------------------------------------------

        /// False when this build cannot scaffold a C# game: no managed runtime, no C# sources (an
        /// installed build never has them), or no templates. NewProjectUnavailableReason() says
        /// which, so the menu entry can explain itself instead of doing nothing.
        bool CanCreateProject() const;
        std::string NewProjectUnavailableReason() const;

        void OpenNewProjectDialog();
        bool IsNewProjectDialogOpen() const;

        /// Draws the modal when it is open. Returns the id of a game created this frame, empty
        /// otherwise — the caller uses it to select the new game in the play toolbar. Call once per
        /// frame, outside any window, the way an ImGui modal wants to be called.
        std::string DrawNewProjectDialog();

        // --- hooks forwarded by EditorGameInstance ---------------------------------------------

        void OnTick(double deltaSeconds);
        /// Draws the running game's own UI inside the given rectangle (the editor's viewport panel,
        /// in ImGui screen coordinates). The game sees that rectangle as its screen: it lays out
        /// against the size, draws offset into it, and is clipped to it — which is what stops a
        /// HUD written for a full window from landing in the corner of the editor and painting
        /// over the panels. A zero-sized rect means "the whole window", for when the viewport
        /// panel is closed.
        ///
        /// Skipped while ejected, so the editor's panels are not covered by a HUD the user is
        /// trying to look past.
        bool OnRenderGameUI(float viewportX, float viewportY, float viewportWidth, float viewportHeight);
        void OnBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                  std::vector<Assets::Model>& models,
                                  std::vector<Assets::FMaterial>& materials,
                                  std::vector<Assets::LightObject>& lights,
                                  std::vector<Assets::AnimationTrack>& tracks);
        void OnSceneLoaded();
        bool TryGetOverrideCamera(Assets::Camera& outCamera) const;
        void SetGamepadInput(int16_t leftStickX,
                             int16_t leftStickY,
                             int16_t rightStickX,
                             int16_t rightStickY,
                             int16_t leftTrigger,
                             int16_t rightTrigger);
        /// The game called Engine.RequestClose(). Returns true when the session handled it by
        /// stopping, which keeps the editor process alive.
        bool OnGameRequestedClose();
        void OnEditorDestroy();

    private:
        struct FImpl;
        std::unique_ptr<FImpl> impl_;
    };
}
