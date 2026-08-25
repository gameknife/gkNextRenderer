#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextDotNet/ManagedGameManifest.hpp"
#include "Modules/NextDotNet/ManagedGameSession.hpp"

namespace Modules::NextDotNet
{
    struct FManagedGameHostOptions
    {
        /// Manifest of the single game this host runs. Empty starts the host idle, which is what
        /// gkNextLauncher does before the player picks something.
        std::string manifestPath;

        /// Window used when there is no manifest to take one from.
        FManagedGameManifest::FWindow window;

        /// Native modules this executable links. Passed to the session so a manifest that needs
        /// something this host does not have is refused up front rather than failing mid-load.
        std::vector<std::string> linkedModules;
    };

    /// The one native shell every C# game shares.
    ///
    /// Before this existed, each managed game carried ~100 lines of hook forwarding that differed
    /// from its neighbour in five places, all of them data (see the design's section 2 table). Now
    /// the differences live in a manifest and the forwarding lives here, so a hook cannot be
    /// forwarded by one game and silently dropped by another — which is exactly what happened to
    /// gamepad input in FlappyCSharp.
    ///
    /// Hosts that need their own UI (the launcher menu, an editor Play button) derive from this and
    /// override the OnHost* hooks; the managed forwarding stays inherited.
    class ManagedGameHostInstance : public NextGameInstanceBase
    {
    public:
        ManagedGameHostInstance(Vulkan::WindowConfig& config,
                                Runtime::Config::Options& options,
                                NextEngine* engine,
                                FManagedGameHostOptions hostOptions);
        ~ManagedGameHostInstance() override;

        void OnInit() override;
        void OnTick(double deltaSeconds) override;
        void OnDestroy() override;
        bool OnRenderUI() override;
        bool OnKey(SDL_Event& event) override;
        bool OnMouseButton(SDL_Event& event) override;
        bool OnGamepadInput(int16_t leftStickX,
                            int16_t leftStickY,
                            int16_t rightStickX,
                            int16_t rightStickY,
                            int16_t leftTrigger,
                            int16_t rightTrigger) override;

        void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials,
                                std::vector<Assets::LightObject>& lights,
                                std::vector<Assets::AnimationTrack>& tracks) override;
        void OnSceneLoaded() override;
        bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

        ManagedGameSession& GetSession() { return session_; }
        const ManagedGameSession& GetSession() const { return session_; }

    protected:
        /// Host-owned UI drawn every frame, before the managed game's own UI. Return true to report
        /// the frame consumed.
        virtual bool OnHostRenderUI() { return false; }

        /// Host-owned input, offered before the managed game sees the event.
        virtual bool OnHostKey(SDL_Event& event) { return false; }

        const FManagedGameHostOptions& GetHostOptions() const { return hostOptions_; }
        const std::optional<FManagedGameManifest>& GetBootManifest() const { return bootManifest_; }

    private:
        FManagedGameHostOptions hostOptions_;
        std::optional<FManagedGameManifest> bootManifest_;
        ManagedGameSession session_;
    };
}
