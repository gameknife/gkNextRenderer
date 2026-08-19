#pragma once

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
        bool toggleMode = false;
        bool respawn = false;
    };

    struct FGeoWalkUIContext
    {
        const std::vector<FGeoTile>* tiles = nullptr;
        int activeTile = -1;
        const FGeoWalker* walker = nullptr;
        const glm::vec3* cameraPosition = nullptr;
        bool followCamera = true;
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
        void ClearSelection() { selectedPoi_ = -1; }

    private:
        void DrawPlaceList(const FGeoTile& tile, const FGeoPoiLayer& poiLayer,
                           const glm::vec3& from, FGeoWalkUIRequest& request);

        bool visible_ = true;
        int selectedPoi_ = -1;
        char filter_[64] = {};
        bool sortByDistance_ = false;
    };
}
