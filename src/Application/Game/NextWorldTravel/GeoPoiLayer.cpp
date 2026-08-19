#include "GeoPoiLayer.h"

#include "NextWorldTravelConfig.hpp"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NextWorldTravel
{
    namespace
    {
        // Category tints. Chosen to stay legible against the urban palette's
        // concrete greys rather than to be pretty in isolation.
        constexpr glm::vec3 kCategoryColors[PoiCategory::kCount] = {
            {1.00f, 0.82f, 0.28f}, // landmark
            {0.42f, 0.78f, 1.00f}, // transport
            {0.94f, 0.55f, 0.86f}, // culture
            {0.58f, 0.86f, 0.50f}, // education
            {1.00f, 0.46f, 0.44f}, // health
            {0.83f, 0.74f, 0.98f}, // worship
            {0.66f, 0.72f, 0.82f}, // civic
            {0.98f, 0.72f, 0.42f}, // commerce
            {0.74f, 0.66f, 0.52f}, // lodging
            {0.46f, 0.90f, 0.66f}, // park
            {0.80f, 0.84f, 0.62f}, // place
            {0.72f, 0.76f, 0.80f}, // other
        };

        ImU32 ToImColor(const glm::vec3& rgb, float alpha)
        {
            return ImGui::GetColorU32(ImVec4(rgb.r, rgb.g, rgb.b, alpha));
        }

        // Street labels fade out with distance; from the air they must not, or
        // the far half of the tile loses its map.
        float LabelAlpha(float distance, bool aerial)
        {
            if (aerial || distance <= Config::kLabelFadeStart)
            {
                return 1.0f;
            }
            const float span = Config::kLabelMaxDistance - Config::kLabelFadeStart;
            return std::clamp(1.0f - (distance - Config::kLabelFadeStart) / span, 0.0f, 1.0f);
        }
    }

    int FGeoPoiLayer::CategoryIndex(const std::string& category)
    {
        for (int i = 0; i < PoiCategory::kCount; ++i)
        {
            if (category == PoiCategory::kAll[static_cast<size_t>(i)])
            {
                return i;
            }
        }
        return PoiCategory::kCount - 1; // "other"
    }

    glm::vec3 FGeoPoiLayer::CategoryColor(const std::string& category)
    {
        return kCategoryColors[CategoryIndex(category)];
    }

    bool FGeoPoiLayer::IsCategoryEnabled(const std::string& category) const
    {
        return categoryEnabled_[static_cast<size_t>(CategoryIndex(category))];
    }

    glm::vec3 FGeoPoiLayer::LabelAnchor(const FGeoPoi& poi)
    {
        const float lift = poi.height > 0.0f ? poi.height + Config::kLabelRoofOffset
                                             : Config::kLabelGroundOffset;
        return {poi.position.x, poi.groundY + lift, poi.position.y};
    }

    void FGeoPoiLayer::OnTerrainReady(std::vector<FGeoPoi>& pois,
                                      const Runtime::TerrainComponent& terrain)
    {
        groundedCount_ = 0;
        if (!terrain.HasData())
        {
            for (FGeoPoi& poi : pois)
            {
                poi.grounded = false;
            }
            return;
        }

        // The heightfield is centred on the tile origin and the sidecar is
        // clipped to the same square, so anything outside the extents is a
        // generator/runtime disagreement rather than an edge case worth
        // guessing at.
        const float halfX = terrain.GetSizeX() * 0.5f;
        const float halfY = terrain.GetSizeY() * 0.5f;
        for (FGeoPoi& poi : pois)
        {
            if (std::abs(poi.position.x) > halfX || std::abs(poi.position.y) > halfY)
            {
                poi.grounded = false;
                continue;
            }
            poi.groundY = terrain.SampleHeight(poi.position.x, poi.position.y);
            poi.grounded = true;
            ++groundedCount_;
        }

        if (groundedCount_ < static_cast<int>(pois.size()))
        {
            SPDLOG_WARN("NextWorldTravel: {} of {} places fall outside the terrain extents and will not be labelled",
                        static_cast<int>(pois.size()) - groundedCount_, pois.size());
        }
    }

    void FGeoPoiLayer::Update(const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition)
    {
        visible_.clear();
        markers_.clear();
        markerScreen_.clear();
        if (!showLabels_)
        {
            return;
        }

        const bool aerial = style_ == ELabelStyle::Aerial;

        struct FCandidate
        {
            int index;
            float distance;
            float rank;
            bool highlighted;
        };
        std::vector<FCandidate> candidates;
        candidates.reserve(pois.size());

        for (size_t i = 0; i < pois.size(); ++i)
        {
            const FGeoPoi& poi = pois[i];
            if (!poi.grounded)
            {
                continue;
            }
            const bool highlighted = static_cast<int>(i) == highlight_;
            if (!highlighted && !IsCategoryEnabled(poi.category))
            {
                continue;
            }
            const glm::vec3 anchor = LabelAnchor(poi);
            const float distance = glm::distance(viewPosition, anchor);
            // From above the whole tile is the subject, so the street rules that
            // hide a minor place until you are next to it would empty the map.
            const float maxDistance = aerial
                                          ? Config::kAerialMarkerMaxDistance
                                          : (poi.rank >= Config::kLabelMinorRank
                                                 ? Config::kLabelMaxDistance
                                                 : Config::kLabelMinorMaxDistance);
            if (!highlighted && distance > maxDistance)
            {
                continue;
            }
            candidates.push_back({static_cast<int>(i), distance, poi.rank, highlighted});
        }

        // Prominence decides who survives the label budget, not proximity:
        // standing in a side street, the tower two blocks away is the label that
        // tells you where you are. Distance only breaks ties, and the place the
        // browser is pointed at always keeps its name.
        std::sort(candidates.begin(), candidates.end(),
                  [](const FCandidate& a, const FCandidate& b)
                  {
                      if (a.highlighted != b.highlighted)
                      {
                          return a.highlighted;
                      }
                      if (a.rank != b.rank)
                      {
                          return a.rank > b.rank;
                      }
                      return a.distance < b.distance;
                  });

        const int budget = aerial ? Config::kAerialMaxLabels : Config::kMaxVisibleLabels;
        const size_t labelled = std::min(candidates.size(),
                                         static_cast<size_t>(std::max(0, budget)));

        // Kept in prominence order: the drawing pass places plates in this
        // order and drops the ones that would land on a plate already placed,
        // so a crowded skyline keeps its landmarks rather than whichever label
        // happened to be drawn last.
        visible_.reserve(labelled);
        for (size_t i = 0; i < labelled; ++i)
        {
            visible_.push_back(candidates[i].index);
        }

        const auto farToNear = [](const FCandidate& a, const FCandidate& b)
        { return a.distance > b.distance; };

        // A marker is cheap and clickable; in the map view every place gets one
        // even when there is no room for its name. On the street the marker set
        // is just the labels, so a label can be clicked too.
        if (aerial)
        {
            std::sort(candidates.begin(), candidates.end(), farToNear);
            markers_.reserve(candidates.size());
            for (const FCandidate& candidate : candidates)
            {
                markers_.push_back(candidate.index);
            }
        }
        else
        {
            markers_ = visible_;
        }
    }

    int FGeoPoiLayer::PickAt(const glm::vec2& screenPosition) const
    {
        int best = -1;
        float bestDistance = Config::kMarkerPickRadius;
        for (size_t i = 0; i < markerScreen_.size() && i < markers_.size(); ++i)
        {
            const glm::vec2& screen = markerScreen_[i];
            if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
            {
                continue;
            }
            const float distance = glm::distance(screen, screenPosition);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = markers_[i];
            }
        }
        return best;
    }

    void FGeoPoiLayer::Draw(const NextGameInstanceBase& gameInstance,
                            const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition)
    {
        markerScreen_.assign(markers_.size(),
                             glm::vec2(std::numeric_limits<float>::quiet_NaN()));
        placedCount_ = 0;
        if (!showLabels_ || markers_.empty())
        {
            return;
        }
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        const bool aerial = style_ == ELabelStyle::Aerial;

        // Pass one: the markers themselves, and the screen positions a click is
        // resolved against.
        for (size_t slot = 0; slot < markers_.size(); ++slot)
        {
            const int index = markers_[slot];
            const FGeoPoi& poi = pois[static_cast<size_t>(index)];
            const glm::vec3 anchor = LabelAnchor(poi);
            ImVec2 screen;
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(gameInstance, anchor, screen))
            {
                continue;
            }
            markerScreen_[slot] = glm::vec2(screen.x, screen.y);

            const float alpha = LabelAlpha(glm::distance(viewPosition, anchor), aerial);
            if (alpha <= 0.02f)
            {
                continue;
            }
            const glm::vec3 color = CategoryColor(poi.category);
            // Rank already encodes height x footprint x category weight, so the
            // marker size is the city's own sense of what matters.
            const float radius = aerial
                                     ? std::clamp(Config::kMarkerMinRadius + poi.rank * 0.32f,
                                                  Config::kMarkerMinRadius, Config::kMarkerMaxRadius)
                                     : 3.0f;
            drawList->AddCircleFilled(screen, radius, ToImColor(color, alpha));
            if (index == highlight_)
            {
                drawList->AddCircle(screen, radius + 6.0f, ToImColor(glm::vec3(1.0f), alpha), 0, 2.0f);
            }
        }

        // Pass two: the names, drawn over every marker so a plate never ends up
        // behind a dot that happens to be nearer. Plates are placed in
        // prominence order, and one that would cover a plate already placed is
        // lifted a row at a time until it finds space: in a CBD a third of the
        // tile's places project into the same hundred pixels, and overlapping
        // plates hide both the names and the city.
        struct FPlate
        {
            float minX, minY, maxX, maxY;
        };
        std::vector<FPlate> placed;
        placed.reserve(visible_.size());
        const auto overlaps = [&placed](const FPlate& plate)
        {
            for (const FPlate& other : placed)
            {
                if (plate.minX < other.maxX && other.minX < plate.maxX &&
                    plate.minY < other.maxY && other.minY < plate.maxY)
                {
                    return true;
                }
            }
            return false;
        };

        for (const int index : visible_)
        {
            const FGeoPoi& poi = pois[static_cast<size_t>(index)];
            const glm::vec3 anchor = LabelAnchor(poi);
            ImVec2 screen;
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(gameInstance, anchor, screen))
            {
                continue;
            }
            const float alpha = LabelAlpha(glm::distance(viewPosition, anchor), aerial);
            if (alpha <= 0.02f)
            {
                continue;
            }

            // The place the browser is pointed at is never decluttered away.
            const bool highlighted = index == highlight_;
            const glm::vec3 color = highlighted
                                        ? glm::mix(CategoryColor(poi.category), glm::vec3(1.0f), 0.45f)
                                        : CategoryColor(poi.category);
            const ImVec2 textSize = ImGui::CalcTextSize(poi.name.c_str());
            ImVec2 textPos(screen.x - textSize.x * 0.5f,
                           screen.y - textSize.y - (aerial ? 9.0f : 10.0f));

            const float rowHeight = textSize.y + 4.0f + Config::kLabelStackGap;
            FPlate plate{textPos.x - 5.0f, textPos.y - 2.0f, textPos.x + textSize.x + 5.0f,
                         textPos.y + textSize.y + 2.0f};
            int row = 0;
            while (!highlighted && row < Config::kLabelStackRows && overlaps(plate))
            {
                plate.minY -= rowHeight;
                plate.maxY -= rowHeight;
                ++row;
            }
            if (!highlighted && overlaps(plate))
            {
                continue;
            }
            placed.push_back(plate);
            const float stacked = -rowHeight * static_cast<float>(row);
            textPos.y += stacked;

            if (!aerial)
            {
                // A pin down to the anchor, so a roof label reads as belonging
                // to the building under it rather than floating over the
                // skyline. From above there is no "under", so no pin.
                drawList->AddLine(ImVec2(screen.x, screen.y), ImVec2(screen.x, plate.maxY),
                                  ToImColor(color, alpha * 0.55f), 1.0f);
            }
            drawList->AddRectFilled(ImVec2(plate.minX, plate.minY), ImVec2(plate.maxX, plate.maxY),
                                    ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.08f,
                                                              alpha * (highlighted ? 0.88f : 0.68f))),
                                    3.0f);
            if (highlighted)
            {
                drawList->AddRect(ImVec2(plate.minX, plate.minY), ImVec2(plate.maxX, plate.maxY),
                                  ToImColor(color, alpha), 3.0f, 0, 1.5f);
            }
            drawList->AddText(textPos, ToImColor(color, alpha), poi.name.c_str());
        }
        placedCount_ = static_cast<int>(placed.size());
    }
}
