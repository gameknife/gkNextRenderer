#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace GeoWalk
{
    // One named place from assets/scad/geo/<tile>/poi.json.
    //
    // The sidecar stores SCAD metres (+x east, +y north), the frame the .scad
    // and the .hmap agree on. `position` here is already converted to the
    // engine's (x, z) = (scad x, -scad y); the ground Y is not in the file at
    // all and is resolved against the loaded terrain, because the heightfield is
    // the only real source for it.
    struct FGeoPoi
    {
        std::string name;
        std::string tag;      // raw OSM key=value, shown in the details panel
        std::string category; // one of GeoWalk::PoiCategory
        std::string source;   // "building" | "area" | "node"
        glm::vec2 position{0.0f};
        float height = 0.0f; // building height in metres, 0 for areas and nodes
        float areaM2 = 0.0f;
        float rank = 0.0f;
        int64_t osmId = 0;

        // Filled in once the terrain is available.
        float groundY = 0.0f;
        bool grounded = false;
    };

    // One generated tile: the scene to load and the places in it.
    struct FGeoTile
    {
        std::string name;      // directory name, e.g. "hk_victoria"
        std::string scenePath; // assets/scad/proc/generated/<name>.scad
        std::string poiPath;   // assets/scad/geo/<name>/poi.json
        glm::dvec2 center{0.0}; // lat, lon
        double sizeM = 0.0;
        std::vector<std::string> attribution;
        std::vector<FGeoPoi> pois; // rank-descending, as written by the generator
        bool poisLoaded = false;
        std::string loadError;
    };

    // Discovers every tile that has both a poi.json and a matching .scad scene.
    // Scans the loose asset tree: these tiles are committed loose files, and a
    // pak has no directory listing to walk. Result is name-sorted.
    std::vector<FGeoTile> DiscoverGeoTiles();

    // Parses the sidecar into tile.pois. Returns false and sets tile.loadError
    // on a missing file, a wrong format tag, or malformed JSON.
    bool LoadTilePois(FGeoTile& tile);

    // Index of the tile whose scene path matches, or -1.
    int FindTileByScene(const std::vector<FGeoTile>& tiles, const std::string& scenePath);
}
