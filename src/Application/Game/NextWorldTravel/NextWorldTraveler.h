#pragma once

#include "Gameplay/Character/NextCharacterController.h"
#include "Gameplay/Sim/CharacterPool.h"

#include <glm/glm.hpp>

#include <random>
#include <string>
#include <vector>

namespace Assets { class Model; class Scene; struct FMaterial; }
namespace Runtime { class TerrainComponent; }
class NextEngine;

namespace NextWorldTravel
{
    enum class EWalkMode
    {
        Roam,  // the character picks its own destinations and walks them
        Player // WASD, through a physics character controller
    };

    // The single ScadRig character that occupies the tile.
    //
    // Two controllers share one visual. Roam mode runs the Sim Kit pool (nav
    // grid A* + path following); player mode runs a physics character
    // controller and copies its position onto the same rig. Both stand on the
    // ground the nav grid sampled, so what the AI can walk and what the player
    // can walk are the same surface — which is the point of the application.
    class FNextWorldTraveler
    {
    public:
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);

        // Builds the nav window, finds a street to stand on, and spawns the rig.
        // Returns false when the tile has no reachable street-level ground.
        bool OnSceneLoaded(Assets::Scene& scene, NextEngine& engine,
                           const Runtime::TerrainComponent* terrain);
        void OnSceneUnloaded();

        void Tick(float deltaSeconds, Assets::Scene& scene);

        // Holds the character still without unspawning it: the route, the nav
        // window and the rig all survive, so resuming continues the walk that
        // was in flight. A nav window rebuild costs about a second, and one
        // landing in the middle of an orbit is the most visible hitch this
        // application has.
        void SetPaused(bool paused) { paused_ = paused; }

        void SetMode(EWalkMode mode);
        EWalkMode Mode() const { return mode_; }
        void ToggleMode() { SetMode(mode_ == EWalkMode::Roam ? EWalkMode::Player : EWalkMode::Roam); }

        // Player intent, in world-space horizontal directions.
        void SetPlayerIntent(const glm::vec3& moveDirection, bool sprint, bool jump);

        // Sends the character to a specific place (a POI, a click). Switches to
        // roam mode, because a scripted destination is a roam destination.
        bool WalkTo(const glm::vec3& worldTarget);

        bool IsSpawned() const { return character_ != nullptr; }
        glm::vec3 Position() const;
        float Yaw() const;
        float Speed() const { return lastSpeed_; }
        bool IsMoving() const;
        const std::string& StatusText() const { return status_; }
        const glm::vec3& RoamTarget() const { return roamTarget_; }
        bool HasRoamTarget() const { return hasRoamTarget_; }

        const NextGameplay::Sim::FCharacterPool& Pool() const { return pool_; }
        // Nav window currently gridded, for the debug overlay.
        glm::vec3 NavWindowMin() const { return navMin_; }
        glm::vec3 NavWindowMax() const { return navMax_; }
        int NavRebuildCount() const { return navRebuilds_; }

        // Ground height an agent stands on at (x, z): the nav grid's sample when
        // inside the window (it follows road decks and piers), the terrain
        // heightfield otherwise.
        float GroundHeight(float x, float z, float fallbackY) const;

    private:
        void ConfigurePool();
        void RebuildNavWindow(Assets::Scene& scene, const glm::vec2& center);
        // Relief-aware floor tolerance for a window (see design §6b).
        float FloorToleranceFor(const glm::vec2& center, float halfSize) const;
        // Finds a nearby dry point for the first nav window. Geo tiles can be
        // centred on a river or harbour, so the tile origin is not guaranteed
        // to be a valid place to start walking.
        bool FindNearestDrySearchCenter(const glm::vec2& searchCenter, glm::vec2& outCenter) const;
        bool FindStreetSpawn(const glm::vec2& searchCenter, glm::vec3& outPosition) const;
        // True when the nav cell at (x, z) sits on the terrain rather than on a
        // roof. The generated tiles make almost every flat roof "walkable", so
        // street level has to be established against the heightfield.
        bool IsStreetLevel(float x, float z, float navGroundY) const;
        bool PickRoamTarget(glm::vec3& outTarget);
        void TickRoam(float deltaSeconds, Assets::Scene& scene);
        void TickPlayer(float deltaSeconds, Assets::Scene& scene);
        void MaybeSlideNavWindow(Assets::Scene& scene);
        void ApplyVisual(float deltaSeconds, Assets::Scene& scene);

        NextGameplay::Sim::FCharacterPool pool_;
        NextGameplay::Sim::FSimCharacter* character_ = nullptr;
        NextCharacterController playerController_;
        NextEngine* engine_ = nullptr;
        Assets::Scene* scene_ = nullptr;
        const Runtime::TerrainComponent* terrain_ = nullptr;

        EWalkMode mode_ = EWalkMode::Roam;
        bool paused_ = false;
        glm::vec3 playerIntent_{0.0f};
        bool playerSprint_ = false;
        bool playerJump_ = false;
        float lastSpeed_ = 0.0f;
        glm::vec3 lastPosition_{0.0f};

        glm::vec3 roamTarget_{0.0f};
        bool hasRoamTarget_ = false;
        float roamElapsed_ = 0.0f;
        float roamPause_ = 0.0f;
        std::mt19937 rng_{20260819u};

        glm::vec3 navMin_{0.0f};
        glm::vec3 navMax_{0.0f};
        int navRebuilds_ = 0;
        std::string status_ = "not spawned";
    };
}
