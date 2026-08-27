#include <catch2/catch_all.hpp>

#include "Engine/Utilities/FileHelper.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// Contract test for assets/geo/<tile>/poi.json, the label sidecar `gnb geo
// build` writes next to terrain.hmap (see docs/designs/geo-city-generation-design.md).
//
// The consumer (NextWorldTravel) anchors every place against the loaded heightfield, so
// a POI outside the tile square silently disappears rather than erroring — which
// makes the *file* the right place to assert. No GPU and no scene load: this
// runs anywhere the repository is checked out.
namespace
{
    namespace fs = std::filesystem;

    // Kept in step with POICategories in tools/gnb/internal/geo/poi.go and with
    // PoiCategory::kAll in the NextWorldTravel runtime. A category outside this set
    // would be drawn in the "other" colour with no warning.
    const std::set<std::string> kCategories = {
        "landmark", "transport", "culture", "education", "health", "worship",
        "civic", "commerce", "lodging", "park", "place", "other"};

    const std::set<std::string> kSources = {"building", "area", "node"};

    std::vector<fs::path> FindPoiFiles()
    {
        std::vector<fs::path> files;
        const fs::path root = Utilities::FileHelper::GetRuntimeFilePath("assets/geo");
        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            return files;
        }
        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
        {
            const fs::path poi = entry.path() / "poi.json";
            if (entry.is_directory() && fs::exists(poi, ec))
            {
                files.push_back(poi);
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    nlohmann::json ReadJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        return nlohmann::json::parse(file, nullptr, false);
    }
}

TEST_CASE("Geo POI sidecars describe places inside their tile", "[Unit][Geo][POI]")
{
    const std::vector<fs::path> files = FindPoiFiles();
    if (files.empty())
    {
        // assets/geo is gitignored: tiles arrive either from `gnb geo make` or
        // from geo.pak, and a clean checkout has neither. This test validates
        // loose sidecars, so with none present there is nothing to check —
        // failing here would only report a missing optional asset.
        SKIP("no loose geo tiles under assets/geo — run `gnb geo make` or `gnb paks fetch geo`");
    }

    for (const fs::path& path : files)
    {
        const std::string label = path.parent_path().filename().string();
        CAPTURE(label);

        const nlohmann::json root = ReadJson(path);
        REQUIRE_FALSE(root.is_discarded());
        REQUIRE(root.is_object());
        CHECK(root.value("format", std::string{}) == "gkgeopoi1");
        CHECK(root.value("tile", std::string{}) == label);

        const double sizeM = root.value("sizeM", 0.0);
        REQUIRE(sizeM > 0.0);
        const double half = sizeM * 0.5;

        // Attribution is a licence obligation, not decoration: the sidecar is a
        // produced work derived from ODbL data.
        const nlohmann::json attribution = root.value("attribution", nlohmann::json::array());
        REQUIRE(attribution.is_array());
        REQUIRE_FALSE(attribution.empty());
        bool hasOsm = false;
        for (const nlohmann::json& line : attribution)
        {
            hasOsm = hasOsm || line.get<std::string>().find("OpenStreetMap") != std::string::npos;
        }
        CHECK(hasOsm);

        const nlohmann::json pois = root.value("pois", nlohmann::json::array());
        REQUIRE(pois.is_array());
        REQUIRE_FALSE(pois.empty());

        double previousRank = std::numeric_limits<double>::max();
        for (const nlohmann::json& poi : pois)
        {
            const std::string name = poi.value("name", std::string{});
            CAPTURE(name);
            CHECK_FALSE(name.empty());
            CHECK(kCategories.count(poi.value("category", std::string{})) == 1);
            CHECK(kSources.count(poi.value("source", std::string{})) == 1);

            REQUIRE(poi.contains("pos"));
            REQUIRE(poi["pos"].is_array());
            REQUIRE(poi["pos"].size() == 2);
            // SCAD metres, tile-centred. Anything outside cannot be anchored to
            // the heightfield and would never be labelled.
            CHECK(std::abs(poi["pos"][0].get<double>()) <= half);
            CHECK(std::abs(poi["pos"][1].get<double>()) <= half);

            // A building's label hangs at its roof, so a nonsense height throws
            // the label into the sky.
            const double height = poi.value("height", 0.0);
            CHECK(height >= 0.0);
            CHECK(height < 1200.0);

            // Rank-descending is what lets the runtime keep the landmarks when
            // it caps how many labels it draws.
            const double rank = poi.value("rank", -1.0);
            CHECK(rank > 0.0);
            CHECK(rank <= previousRank);
            previousRank = rank;
        }
    }
}

TEST_CASE("Geo POI sidecars name each place once", "[Unit][Geo][POI]")
{
    for (const fs::path& path : FindPoiFiles())
    {
        const std::string label = path.parent_path().filename().string();
        CAPTURE(label);
        const nlohmann::json root = ReadJson(path);
        REQUIRE_FALSE(root.is_discarded());

        // OSM routinely carries a venue twice — once as the building way, once
        // as a node inside it. Two labels on one façade is the visible symptom.
        struct FPlace
        {
            std::string name;
            double x;
            double y;
        };
        std::vector<FPlace> places;
        for (const nlohmann::json& poi : root.value("pois", nlohmann::json::array()))
        {
            places.push_back({poi.value("name", std::string{}),
                              poi["pos"][0].get<double>(), poi["pos"][1].get<double>()});
        }

        for (size_t i = 0; i < places.size(); ++i)
        {
            for (size_t j = i + 1; j < places.size(); ++j)
            {
                if (places[i].name != places[j].name)
                {
                    continue;
                }
                const double distance = std::hypot(places[i].x - places[j].x,
                                                   places[i].y - places[j].y);
                CAPTURE(places[i].name, distance);
                // Two genuinely distinct places can share a name (a chain, a
                // pair of station entrances); co-located ones cannot.
                CHECK(distance > 60.0);
            }
        }
    }
}
