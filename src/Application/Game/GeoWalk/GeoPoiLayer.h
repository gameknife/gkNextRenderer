#pragma once

#include "GeoTileCatalog.h"

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

namespace Runtime { class TerrainComponent; }
class NextGameInstanceBase;

namespace GeoWalk
{
    // Categories the generator emits, in its canonical order. Kept in step with
    // POICategories in tools/gnb/internal/geo/poi.go; an unrecognised category
    // falls back to "other" rather than being dropped.
    namespace PoiCategory
    {
        inline constexpr std::array<const char*, 12> kAll = {
            "landmark", "transport", "culture", "education", "health",
            "worship", "civic", "commerce", "lodging", "park", "place", "other"};
        inline constexpr int kCount = static_cast<int>(kAll.size());
    }

    // Owns the runtime view of one tile's places: where each label hangs in the
    // world, which ones are currently worth drawing, and the on-screen layer.
    class FGeoPoiLayer
    {
    public:
        FGeoPoiLayer() { categoryEnabled_.fill(true); }

        // Resolves every label's ground height against the loaded terrain. POIs
        // whose anchor falls outside the heightfield keep grounded == false and
        // are skipped: a label at y = 0 on a 480 m plateau is worse than none.
        void OnTerrainReady(std::vector<FGeoPoi>& pois, const Runtime::TerrainComponent& terrain);

        // Recomputes the visible set for this frame's viewpoint.
        void Update(const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition);

        // Draws the labels into ImGui's foreground list.
        void Draw(const NextGameInstanceBase& gameInstance, const std::vector<FGeoPoi>& pois,
                  const glm::vec3& viewPosition) const;

        // World-space anchor a label hangs at (roof for buildings, a fixed lift
        // above ground for everything else).
        static glm::vec3 LabelAnchor(const FGeoPoi& poi);
        static int CategoryIndex(const std::string& category);
        static glm::vec3 CategoryColor(const std::string& category);

        bool IsCategoryEnabled(const std::string& category) const;
        bool& CategoryEnabled(int index) { return categoryEnabled_[static_cast<size_t>(index)]; }
        bool CategoryEnabled(int index) const { return categoryEnabled_[static_cast<size_t>(index)]; }

        bool& ShowLabels() { return showLabels_; }
        bool ShowLabels() const { return showLabels_; }
        int VisibleCount() const { return static_cast<int>(visible_.size()); }
        int GroundedCount() const { return groundedCount_; }
        const std::vector<int>& Visible() const { return visible_; }

    private:
        std::vector<int> visible_; // indices into the tile's POI list, nearest first
        std::array<bool, PoiCategory::kCount> categoryEnabled_{};
        bool showLabels_ = true;
        int groundedCount_ = 0;
    };
}
