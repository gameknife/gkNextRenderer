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

    // How the places are presented. From the street a label is a sign hanging
    // over a building; from the air the tile is a map, and every place is a
    // marker you can click even when there is no room for its name.
    enum class ELabelStyle
    {
        Street,
        Aerial
    };

    // Owns the runtime view of one tile's places: where each label hangs in the
    // world, which ones are currently worth drawing, and the on-screen layer.
    class FGeoPoiLayer
    {
    public:
        FGeoPoiLayer() { categoryEnabled_.fill(true); }

        void SetStyle(ELabelStyle style) { style_ = style; }
        ELabelStyle Style() const { return style_; }
        // The place the browser is currently pointed at. It is always drawn,
        // whatever the distance rules and category filters say, because losing
        // the label of the thing you just asked to look at is absurd.
        void SetHighlight(int poiIndex) { highlight_ = poiIndex; }
        int Highlight() const { return highlight_; }

        // Resolves every label's ground height against the loaded terrain. POIs
        // whose anchor falls outside the heightfield keep grounded == false and
        // are skipped: a label at y = 0 on a 480 m plateau is worse than none.
        void OnTerrainReady(std::vector<FGeoPoi>& pois, const Runtime::TerrainComponent& terrain);

        // Recomputes the visible set for this frame's viewpoint.
        void Update(const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition);

        // Draws the markers and labels into ImGui's background list, and caches
        // where each marker landed on screen so it can be clicked.
        void Draw(const NextGameInstanceBase& gameInstance, const std::vector<FGeoPoi>& pois,
                  const glm::vec3& viewPosition);

        // Index of the marker under a screen position, or -1. Uses the previous
        // frame's projection, which is what the user actually clicked on.
        int PickAt(const glm::vec2& screenPosition) const;

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
        // Labels the budget allowed. PlacedCount is how many actually reached
        // the screen after decluttering, which is the number worth reporting.
        int VisibleCount() const { return static_cast<int>(visible_.size()); }
        int PlacedCount() const { return placedCount_; }
        int MarkerCount() const { return static_cast<int>(markers_.size()); }
        int GroundedCount() const { return groundedCount_; }
        const std::vector<int>& Visible() const { return visible_; }

    private:
        // Every place worth a dot this frame, far to near.
        std::vector<int> markers_;
        // The subset that also gets its name drawn, far to near.
        std::vector<int> visible_;
        // Parallel to markers_; off-screen entries are flagged with a NaN x.
        std::vector<glm::vec2> markerScreen_;
        std::array<bool, PoiCategory::kCount> categoryEnabled_{};
        ELabelStyle style_ = ELabelStyle::Street;
        bool showLabels_ = true;
        int highlight_ = -1;
        int placedCount_ = 0;
        int groundedCount_ = 0;
    };
}
