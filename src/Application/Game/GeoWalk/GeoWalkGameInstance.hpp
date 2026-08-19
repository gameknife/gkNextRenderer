#pragma once

#include "GeoPoiLayer.h"
#include "GeoTileCatalog.h"
#include "GeoWalkConfig.hpp"
#include "GeoWalkUI.h"
#include "GeoWalker.h"

#include "Engine/Runtime/GameInstance.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Runtime { class TerrainComponent; }

// Loads the city tiles `gnb geo` generates from real geographic data, labels
// the places OpenStreetMap names in them, and puts a ScadRig character on the
// walkable ground — either roaming on its own or under WASD.
//
// See docs/AGENT_GUIDE/GeoWalk.md and
// docs/designs/geo-city-generation-design.md.
class GeoWalkGameInstance : public NextGameInstanceBase
{
public:
    GeoWalkGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                        NextEngine* engine);
    ~GeoWalkGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OnRenderUI() override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& registry) override;

private:
    enum class ECameraMode
    {
        Follow, // orbits the character
        Free    // detached, WASD flies the camera
    };

    void RequestTile(int index);
    void TryResolveTerrain();
    void SpawnWalker();
    void UpdateCamera(float deltaSeconds);
    // Distance the follow camera may actually pull back to without ending up
    // inside a building. A downtown tile has a facade within a few metres of
    // most pavements, so this is not an edge case.
    float ResolveFollowDistance(const glm::vec3& focus, float desired) const;
    void UpdatePlayerIntent();
    void ApplyUIRequest(const GeoWalk::FGeoWalkUIRequest& request);
    void LookAtPoi(const GeoWalk::FGeoPoi& poi);
    glm::vec3 ViewForward() const;
    glm::vec3 ViewRight() const;
    glm::vec3 CameraPosition() const;
    glm::mat4 ViewMatrix() const;
    const GeoWalk::FGeoTile* ActiveTile() const;
    GeoWalk::FGeoTile* ActiveTile();

    std::vector<GeoWalk::FGeoTile> tiles_;
    int activeTile_ = -1;
    int pendingTile_ = -1;

    Runtime::TerrainComponent* terrain_ = nullptr;
    bool sceneReady_ = false;
    bool walkerSpawned_ = false;

    GeoWalk::FGeoWalker walker_;
    GeoWalk::FGeoPoiLayer poiLayer_;
    GeoWalk::FGeoWalkUI ui_;

    // Camera
    ECameraMode cameraMode_ = ECameraMode::Follow;
    float yaw_ = 0.0f;
    float pitch_ = GeoWalk::Config::kSpawnPitch;
    float followDistance_ = GeoWalk::Config::kFollowDistance;
    float resolvedFollowDistance_ = GeoWalk::Config::kFollowDistance;
    glm::vec3 freeCameraPosition_{0.0f, 60.0f, 0.0f};
    glm::vec3 smoothedFocus_{0.0f};
    bool focusInitialized_ = false;

    // Input
    bool keyForward_ = false;
    bool keyBack_ = false;
    bool keyLeft_ = false;
    bool keyRight_ = false;
    bool keySprint_ = false;
    bool keyUp_ = false;
    bool keyDown_ = false;
    bool mouseLookActive_ = false;
    bool resetMouse_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};

    float frameMs_ = 0.0f;
};
