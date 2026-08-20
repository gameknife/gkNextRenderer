#include "GeoTileCatalog.h"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadShared.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
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
        // Mirrors geo.GeoAssetRoot in tools/gnb/internal/geo/cache.go. Every
        // artefact of a tile lives in one directory under here so a single pak
        // (assets/paks/geo.pak) can carry the whole set.
        constexpr const char* kGeoAssetDir = "assets/geo";

        std::string ScenePathFor(const std::string& tileName)
        {
            return std::string(kGeoAssetDir) + "/" + tileName + "/" + tileName + ".scad";
        }

        std::string PoiPathFor(const std::string& tileName)
        {
            return std::string(kGeoAssetDir) + "/" + tileName + "/poi.json";
        }

        // Tile names from loose files on disk. Present while iterating on the
        // generator, before (or instead of) packing.
        void CollectLooseTileNames(std::set<std::string>& names)
        {
            const fs::path root = Utilities::FileHelper::GetRuntimeFilePath(kGeoAssetDir);
            std::error_code ec;
            if (!fs::is_directory(root, ec))
            {
                return;
            }
            for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
            {
                if (entry.is_directory())
                {
                    names.insert(entry.path().filename().string());
                }
            }
        }

        // Tile names from geo.pak. assets/geo is gitignored, so for anyone who
        // did not run the generator themselves this is the only source — and a
        // mounted pak is invisible to directory_iterator.
        void CollectPackedTileNames(std::set<std::string>& names)
        {
            const Utilities::Package::FPackageFileSystem* pak =
                Utilities::Package::FPackageFileSystem::TryGetInstance();
            if (pak == nullptr)
            {
                return;
            }
            const std::string prefix = std::string(kGeoAssetDir) + "/";
            for (const std::string& entry : pak->ListMountedEntries(prefix))
            {
                const size_t begin = prefix.size();
                const size_t slash = entry.find('/', begin);
                if (slash != std::string::npos && slash > begin)
                {
                    names.insert(entry.substr(begin, slash - begin));
                }
            }
        }

        // A tile is usable only with both halves: the scene to load and the
        // labels to draw on it. ScadReadAsset resolves pak first, then loose
        // files, so this one check covers both origins.
        bool TileIsComplete(const std::string& tileName, std::string& outMissing)
        {
            std::vector<uint8_t> probe;
            for (const std::string& path : {ScenePathFor(tileName), PoiPathFor(tileName)})
            {
                if (!Assets::Scad::ScadReadAsset(path, probe) || probe.empty())
                {
                    outMissing = path;
                    return false;
                }
            }
            return true;
        }
    }

    std::vector<FGeoTile> DiscoverGeoTiles()
    {
        std::set<std::string> names;
        CollectLooseTileNames(names);
        CollectPackedTileNames(names);
        if (names.empty())
        {
            SPDLOG_WARN("NextWorldTravel: no geo tiles under '{}' — run `gnb geo make`, "
                        "or `gnb paks fetch geo` for the published ones",
                        kGeoAssetDir);
            return {};
        }

        std::vector<FGeoTile> tiles;
        tiles.reserve(names.size());
        for (const std::string& tileName : names)
        {
            std::string missing;
            if (!TileIsComplete(tileName, missing))
            {
                SPDLOG_WARN("NextWorldTravel: tile '{}' is incomplete — '{}' is missing; "
                            "regenerate it with `gnb geo make --name {}`",
                            tileName, missing, tileName);
                continue;
            }
            FGeoTile tile;
            tile.name = tileName;
            tile.scenePath = ScenePathFor(tileName);
            tile.poiPath = PoiPathFor(tileName);
            tiles.push_back(std::move(tile));
        }

        // std::set already orders the names; the tiles inherit that order.
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
