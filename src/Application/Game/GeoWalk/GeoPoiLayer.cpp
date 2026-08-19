#include "GeoPoiLayer.h"

#include "GeoWalkConfig.hpp"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace GeoWalk
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
            SPDLOG_WARN("GeoWalk: {} of {} places fall outside the terrain extents and will not be labelled",
                        static_cast<int>(pois.size()) - groundedCount_, pois.size());
        }
    }

    void FGeoPoiLayer::Update(const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition)
    {
        visible_.clear();
        if (!showLabels_)
        {
            return;
        }

        struct FCandidate
        {
            int index;
            float distance;
            float rank;
        };
        std::vector<FCandidate> candidates;
        candidates.reserve(pois.size());

        for (size_t i = 0; i < pois.size(); ++i)
        {
            const FGeoPoi& poi = pois[i];
            if (!poi.grounded || !IsCategoryEnabled(poi.category))
            {
                continue;
            }
            const glm::vec3 anchor = LabelAnchor(poi);
            const float distance = glm::distance(viewPosition, anchor);
            const float maxDistance = poi.rank >= Config::kLabelMinorRank
                                          ? Config::kLabelMaxDistance
                                          : Config::kLabelMinorMaxDistance;
            if (distance > maxDistance)
            {
                continue;
            }
            candidates.push_back({static_cast<int>(i), distance, poi.rank});
        }

        // Prominence decides who survives the budget, not proximity: standing in
        // a side street, the tower two blocks away is the label that tells you
        // where you are. Distance only breaks ties.
        std::sort(candidates.begin(), candidates.end(),
                  [](const FCandidate& a, const FCandidate& b)
                  {
                      if (a.rank != b.rank)
                      {
                          return a.rank > b.rank;
                      }
                      return a.distance < b.distance;
                  });
        if (static_cast<int>(candidates.size()) > Config::kMaxVisibleLabels)
        {
            candidates.resize(Config::kMaxVisibleLabels);
        }

        // Draw far to near so nearer labels overlap the ones behind them.
        std::sort(candidates.begin(), candidates.end(),
                  [](const FCandidate& a, const FCandidate& b) { return a.distance > b.distance; });
        visible_.reserve(candidates.size());
        for (const FCandidate& candidate : candidates)
        {
            visible_.push_back(candidate.index);
        }
    }

    void FGeoPoiLayer::Draw(const NextGameInstanceBase& gameInstance,
                            const std::vector<FGeoPoi>& pois, const glm::vec3& viewPosition) const
    {
        if (!showLabels_ || visible_.empty())
        {
            return;
        }
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        for (const int index : visible_)
        {
            const FGeoPoi& poi = pois[static_cast<size_t>(index)];
            const glm::vec3 anchor = LabelAnchor(poi);
            ImVec2 screen;
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(gameInstance, anchor, screen))
            {
                continue;
            }

            const float distance = glm::distance(viewPosition, anchor);
            float alpha = 1.0f;
            if (distance > Config::kLabelFadeStart)
            {
                const float span = Config::kLabelMaxDistance - Config::kLabelFadeStart;
                alpha = std::clamp(1.0f - (distance - Config::kLabelFadeStart) / span, 0.0f, 1.0f);
            }
            if (alpha <= 0.02f)
            {
                continue;
            }

            const glm::vec3 color = CategoryColor(poi.category);
            const ImVec2 textSize = ImGui::CalcTextSize(poi.name.c_str());
            const ImVec2 textPos(screen.x - textSize.x * 0.5f, screen.y - textSize.y - 10.0f);

            // A pin down to the anchor, so a roof label reads as belonging to
            // the building under it rather than floating over the skyline.
            drawList->AddLine(ImVec2(screen.x, screen.y),
                              ImVec2(screen.x, textPos.y + textSize.y),
                              ToImColor(color, alpha * 0.55f), 1.0f);
            drawList->AddCircleFilled(ImVec2(screen.x, screen.y), 3.0f, ToImColor(color, alpha));
            drawList->AddRectFilled(ImVec2(textPos.x - 5.0f, textPos.y - 2.0f),
                                    ImVec2(textPos.x + textSize.x + 5.0f, textPos.y + textSize.y + 2.0f),
                                    ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.08f, alpha * 0.68f)), 3.0f);
            drawList->AddText(textPos, ToImColor(color, alpha), poi.name.c_str());
        }
    }
}
