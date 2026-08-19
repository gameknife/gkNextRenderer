#pragma once

#include "GeoCameraDirector.h"
#include "GeoPoiLayer.h"
#include "GeoTileCatalog.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace GeoWalk
{
    class FGeoWalker;

    // What the HUD is allowed to ask the application to do this frame.
    struct FGeoWalkUIRequest
    {
        int loadTileIndex = -1;
        int walkToPoiIndex = -1;
        int lookAtPoiIndex = -1;
        int focusPoiIndex = -1;
        bool setViewMode = false;
        EViewMode viewMode = EViewMode::Walk;
        bool focusNext = false;
        bool focusPrev = false;
        bool toggleTour = false;
        bool toggleMode = false; // roam <-> player, within Walk
        bool respawn = false;
    };

    struct FGeoWalkUIContext
    {
        const std::vector<FGeoTile>* tiles = nullptr;
        int activeTile = -1;
        const FGeoWalker* walker = nullptr;
        const glm::vec3* cameraPosition = nullptr;
        // The camera and the tour dwell are edited in place: a slider that has
        // to round-trip through a request struct cannot be dragged.
        FGeoCameraDirector* camera = nullptr;
        float* tourDwellSeconds = nullptr;
        EViewMode viewMode = EViewMode::Walk;
        bool followCamera = true;
        int focusPoi = -1;
        int focusOrdinal = 0; // 1-based position in the browse order
        int focusTotal = 0;
        bool tourActive = false;
        float tourProgress = 0.0f; // 0..1 of the dwell left on this place
        float frameMs = 0.0f;
    };

    class FGeoWalkUI
    {
    public:
        // Returns the actions the user asked for; the caller performs them.
        FGeoWalkUIRequest Draw(const FGeoWalkUIContext& context, FGeoPoiLayer& poiLayer);

        bool& Visible() { return visible_; }
        bool Visible() const { return visible_; }
        int SelectedPoi() const { return selectedPoi_; }
        void SetSelectedPoi(int index) { selectedPoi_ = index; }
        void ClearSelection() { selectedPoi_ = -1; }

    private:
        void DrawViewModeBar(const FGeoWalkUIContext& context, FGeoWalkUIRequest& request);
        void DrawWalkPanel(const FGeoWalkUIContext& context, FGeoWalkUIRequest& request);
        void DrawAerialPanel(const FGeoWalkUIContext& context, const FGeoPoiLayer& poiLayer,
                             FGeoWalkUIRequest& request);
        void DrawFocusPanel(const FGeoWalkUIContext& context, const FGeoTile* tile,
                            FGeoWalkUIRequest& request);
        void DrawPlaceList(const FGeoTile& tile, const FGeoPoiLayer& poiLayer,
                           const FGeoWalkUIContext& context, const glm::vec3& from,
                           FGeoWalkUIRequest& request);

        bool visible_ = true;
        int selectedPoi_ = -1;
        char filter_[64] = {};
        bool sortByDistance_ = false;
    };
}
