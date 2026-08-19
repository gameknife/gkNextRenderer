#include "GeoTileCatalog.h"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadShared.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace NextWorldTravel
{
    namespace
    {
        // The sidecar's version tag, written by `gnb geo build`. Refusing an
        // unknown one is deliberate: a silently mis-parsed POI file puts labels
        // in the wrong place, which reads as an engine bug.
        constexpr const char* kPoiFormat = "gkgeopoi1";
        constexpr const char* kGeoAssetDir = "assets/scad/geo";
        constexpr const char* kSceneDir = "assets/scad/proc/generated";

        std::string ScenePathFor(const std::string& tileName)
        {
            return std::string(kSceneDir) + "/" + tileName + ".scad";
        }

        std::string PoiPathFor(const std::string& tileName)
        {
            return std::string(kGeoAssetDir) + "/" + tileName + "/poi.json";
        }
    }

    std::vector<FGeoTile> DiscoverGeoTiles()
    {
        std::vector<FGeoTile> tiles;
        const fs::path root = Utilities::FileHelper::GetRuntimeFilePath(kGeoAssetDir);
        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            SPDLOG_WARN("NextWorldTravel: no geo tiles at '{}' — run `gnb geo make` first", root.string());
            return tiles;
        }

        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            const std::string tileName = entry.path().filename().string();
            const fs::path poi = entry.path() / "poi.json";
            if (!fs::exists(poi, ec))
            {
                // A tile generated before the POI stage existed. Nothing to
                // label, so nothing for this application to show.
                SPDLOG_WARN("NextWorldTravel: tile '{}' has no poi.json — regenerate it with `gnb geo build`",
                            tileName);
                continue;
            }
            const fs::path scene = Utilities::FileHelper::GetRuntimeFilePath(ScenePathFor(tileName));
            if (!fs::exists(scene, ec))
            {
                SPDLOG_WARN("NextWorldTravel: tile '{}' has no scene at '{}'", tileName, scene.string());
                continue;
            }
            FGeoTile tile;
            tile.name = tileName;
            tile.scenePath = ScenePathFor(tileName);
            tile.poiPath = PoiPathFor(tileName);
            tiles.push_back(std::move(tile));
        }

        std::sort(tiles.begin(), tiles.end(),
                  [](const FGeoTile& a, const FGeoTile& b) { return a.name < b.name; });
        return tiles;
    }

    bool LoadTilePois(FGeoTile& tile)
    {
        tile.pois.clear();
        tile.poisLoaded = false;
        tile.loadError.clear();

        std::vector<uint8_t> bytes;
        // Same resolution the .scad's own .hmap reference uses: package file
        // system first, loose file second. A tile packed into a pak keeps
        // working without a second code path.
        if (!Assets::Scad::ScadReadAsset(tile.poiPath, bytes) || bytes.empty())
        {
            tile.loadError = "cannot read " + tile.poiPath;
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            tile.loadError = tile.poiPath + ": not valid JSON";
            return false;
        }
        if (root.value("format", std::string{}) != kPoiFormat)
        {
            tile.loadError = tile.poiPath + ": unknown format '" +
                             root.value("format", std::string{}) + "', expected " + kPoiFormat;
            return false;
        }

        if (root.contains("center") && root["center"].is_array() && root["center"].size() == 2)
        {
            tile.center = {root["center"][0].get<double>(), root["center"][1].get<double>()};
        }
        tile.sizeM = root.value("sizeM", 0.0);
        tile.attribution.clear();
        for (const nlohmann::json& line : root.value("attribution", nlohmann::json::array()))
        {
            if (line.is_string())
            {
                tile.attribution.push_back(line.get<std::string>());
            }
        }

        for (const nlohmann::json& entry : root.value("pois", nlohmann::json::array()))
        {
            if (!entry.is_object() || !entry.contains("pos") || !entry["pos"].is_array() ||
                entry["pos"].size() != 2)
            {
                continue;
            }
            FGeoPoi poi;
            poi.name = entry.value("name", std::string{});
            if (poi.name.empty())
            {
                continue;
            }
            poi.tag = entry.value("tag", std::string{});
            poi.category = entry.value("category", std::string{"other"});
            poi.source = entry.value("source", std::string{"node"});
            const float scadX = entry["pos"][0].get<float>();
            const float scadY = entry["pos"][1].get<float>();
            // SCAD +y is north; the engine's +z runs south.
            poi.position = {scadX, -scadY};
            poi.height = entry.value("height", 0.0f);
            poi.areaM2 = entry.value("areaM2", 0.0f);
            poi.rank = entry.value("rank", 0.0f);
            poi.osmId = entry.value("id", static_cast<int64_t>(0));
            tile.pois.push_back(std::move(poi));
        }

        tile.poisLoaded = true;
        SPDLOG_INFO("NextWorldTravel: tile '{}' — {} places from {}", tile.name, tile.pois.size(), tile.poiPath);
        return true;
    }

    int FindTileByScene(const std::vector<FGeoTile>& tiles, const std::string& scenePath)
    {
        const std::string normalized = fs::path(scenePath).lexically_normal().generic_string();
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            if (fs::path(tiles[i].scenePath).lexically_normal().generic_string() == normalized)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}
