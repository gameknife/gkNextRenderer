#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextDotNet/ManagedGameTemplate.hpp"

#include <array>

namespace Modules::NextDotNet
{
    class ManagedGameSession;

    /// What one Draw call did. Everything is false on an ordinary frame.
    struct FNewGameProjectOutcome
    {
        /// True on the single frame a project finished being created. The host should rescan its
        /// manifests and select `gameId`.
        bool created = false;
        std::string gameId;
        /// The project was also published, so it can be played without another step.
        bool built = false;
    };

    /// The "new game project" modal, shared by gkNextLauncher and gkNextEditor.
    ///
    /// One implementation rather than one per host: the two would drift, and the half that drifts
    /// is always validation — which is the half that decides whether a directory gets written.
    ///
    /// Drives Modules::NextDotNet::CreateManagedGame, then ManagedGameSession::RebuildGame to
    /// publish. Both block for long enough to matter (a publish takes seconds), so the dialog runs
    /// them one frame *after* it has drawn the frame that says what it is doing.
    class FNewGameProjectDialog
    {
    public:
        /// Rescans the templates and resets the form. Cheap enough to call on every click.
        void Open();
        void Close();
        bool IsOpen() const { return open_; }

        /// Draws the modal when open, and does nothing otherwise. `session` publishes the created
        /// project; pass nullptr from a host that cannot build, and the checkbox disappears.
        FNewGameProjectOutcome Draw(ManagedGameSession* session);

        /// Why this build cannot create a project, or empty when it can. An installed build has no
        /// C# sources to write into, and a build with no templates has nothing to write.
        static std::string UnavailableReason();

    private:
        /// The create-then-build sequence, spread over frames so the UI can narrate it.
        enum class EPhase
        {
            /// Filling in the form.
            Editing,
            /// A frame has been drawn saying "creating"; the work runs at the top of the next one.
            Working,
            /// Finished. The panel shows what was written and what to do next.
            Done,
        };

        void ResetForm();
        void PerformWork(ManagedGameSession* session);
        void SyncDerivedNames();
        const FGameTemplate* SelectedTemplate() const;

        bool open_ = false;
        bool requestOpen_ = false;
        EPhase phase_ = EPhase::Editing;

        std::vector<FGameTemplate> templates_;
        int selectedTemplate_ = 0;

        std::array<char, 64> projectName_{};
        std::array<char, 96> displayName_{};
        std::array<char, 64> gameId_{};
        /// Until the user types in them, the display name and the id follow the project name.
        /// Tracking this is what stops a typed id from being overwritten by the next keystroke in
        /// the name field.
        bool displayNameEdited_ = false;
        bool gameIdEdited_ = false;

        bool buildAfterCreate_ = true;
        std::string validationError_;
        std::string unavailableReason_;

        FNewGameResult result_;
        std::string buildError_;
        bool built_ = false;
    };
}
