#pragma once

#include "GeoCameraDirector.h"
#include "GeoPoiLayer.h"
#include "GeoTileCatalog.h"
#include "NextWorldTravelConfig.hpp"
#include "NextWorldTravelUI.h"
#include "NextWorldTraveler.h"

#include "Engine/Runtime/GameInstance.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Runtime { class TerrainComponent; }

// A browser for the city tiles `gnb geo` generates from real geographic data.
//
// One tile can be looked at three ways, and the application is the switch
// between them: Walk puts a ScadRig character on the street (roaming on its own
// or under WASD), Aerial turns the tile into a map of the places OpenStreetMap
// named in it, and Focus orbits one of those places — by hand, or as a tour
// that steps through them in order of prominence.
//
// See docs/AGENT_GUIDE/NextWorldTravel.md and
// docs/designs/geo-city-generation-design.md.
class NextWorldTravelGameInstance : public NextGameInstanceBase
{
public:
    NextWorldTravelGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                        NextEngine* engine);
    ~NextWorldTravelGameInstance() override = default;

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
    void RequestTile(int index);
    void TryResolveTerrain();
    void SpawnWalker();
    void UpdatePlayerIntent();
    void ApplyUIRequest(const NextWorldTravel::FNextWorldTravelUIRequest& request);

    // ---- Browsing --------------------------------------------------------
    void SetViewMode(NextWorldTravel::EViewMode mode);
    // Points the orbit at a place, entering Focus mode if it is not already on.
    void FocusPoi(int poiIndex);
    // Steps through the browse order; +1 is the next place, -1 the previous.
    void FocusStep(int delta);
    // The places the browser steps through: everything anchored and not
    // filtered out, most prominent first — the same order the sidecar is
    // written in, minus whatever the category checkboxes are hiding.
    std::vector<int> BuildBrowseOrder() const;
    void UpdateTour(float deltaSeconds);
    NextWorldTravel::FFocusSubject MakeFocusSubject(const NextWorldTravel::FGeoPoi& poi) const;
    NextWorldTravel::FCameraWorld MakeCameraWorld() const;
    // Draws the character's position into the aerial view, so the map says
    // where walking would resume from.
    void DrawWalkerMarker() const;

    const NextWorldTravel::FGeoTile* ActiveTile() const;
    NextWorldTravel::FGeoTile* ActiveTile();
    glm::vec3 CameraPosition() const { return camera_.EyePosition(); }

    std::vector<NextWorldTravel::FGeoTile> tiles_;
    int activeTile_ = -1;
    int pendingTile_ = -1;

    Runtime::TerrainComponent* terrain_ = nullptr;
    bool sceneReady_ = false;
    bool walkerSpawned_ = false;

    NextWorldTravel::FNextWorldTraveler walker_;
    NextWorldTravel::FGeoPoiLayer poiLayer_;
    NextWorldTravel::FNextWorldTravelUI ui_;
    NextWorldTravel::FGeoCameraDirector camera_;

    // Browsing state
    int focusPoi_ = -1;
    bool tourActive_ = false;
    float tourTimer_ = 0.0f;
    float tourDwell_ = NextWorldTravel::Config::kTourDwellSeconds;

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
