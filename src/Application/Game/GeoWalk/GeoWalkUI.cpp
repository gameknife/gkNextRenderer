#include "GeoWalkUI.h"

#include "GeoWalkConfig.hpp"
#include "GeoWalker.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace GeoWalk
{
    namespace
    {
        bool ContainsNoCase(const std::string& haystack, const char* needle)
        {
            if (needle == nullptr || needle[0] == '\0')
            {
                return true;
            }
            const auto it = std::search(
                haystack.begin(), haystack.end(), needle, needle + std::strlen(needle),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                            std::tolower(static_cast<unsigned char>(b)); });
            return it != haystack.end();
        }

        const char* ModeName(EWalkMode mode)
        {
            return mode == EWalkMode::Roam ? "Roam (AI)" : "Player (WASD)";
        }
    }

    FGeoWalkUIRequest FGeoWalkUI::Draw(const FGeoWalkUIContext& context, FGeoPoiLayer& poiLayer)
    {
        FGeoWalkUIRequest request;
        if (!visible_ || context.tiles == nullptr)
        {
            return request;
        }

        const std::vector<FGeoTile>& tiles = *context.tiles;
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(390.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("GeoWalk", &visible_))
        {
            ImGui::End();
            return request;
        }

        // ---- Tile ---------------------------------------------------------
        const FGeoTile* tile = (context.activeTile >= 0 &&
                                context.activeTile < static_cast<int>(tiles.size()))
                                   ? &tiles[static_cast<size_t>(context.activeTile)]
                                   : nullptr;
        const char* preview = tile != nullptr ? tile->name.c_str() : "(no tile)";
        if (ImGui::BeginCombo("Tile", preview))
        {
            for (size_t i = 0; i < tiles.size(); ++i)
            {
                const bool selected = static_cast<int>(i) == context.activeTile;
                if (ImGui::Selectable(tiles[i].name.c_str(), selected) && !selected)
                {
                    request.loadTileIndex = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        if (tile != nullptr)
        {
            ImGui::TextDisabled("%.5f, %.5f — %.0f m tile, %zu places",
                                tile->center.x, tile->center.y, tile->sizeM, tile->pois.size());
            if (!tile->loadError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tile->loadError.c_str());
            }
        }

        ImGui::Separator();

        // ---- Character ----------------------------------------------------
        if (context.walker != nullptr)
        {
            const FGeoWalker& walker = *context.walker;
            ImGui::Text("Mode: %s", ModeName(walker.Mode()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Switch (F)"))
            {
                request.toggleMode = true;
            }
            if (walker.IsSpawned())
            {
                const glm::vec3 p = walker.Position();
                ImGui::Text("Position  %.1f, %.1f, %.1f", p.x, p.y, p.z);
                ImGui::Text("Speed     %.2f m/s", walker.Speed());
                ImGui::TextDisabled("%s", walker.StatusText().c_str());
                ImGui::TextDisabled("nav window %.0f m, rebuilt %d time(s)",
                                    (walker.NavWindowMax().x - walker.NavWindowMin().x),
                                    walker.NavRebuildCount());
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "not spawned: %s",
                                   walker.StatusText().c_str());
            }
            if (ImGui::SmallButton("Respawn"))
            {
                request.respawn = true;
            }
        }
        ImGui::TextDisabled("%.1f ms/frame", context.frameMs);

        ImGui::Separator();

        // ---- Labels -------------------------------------------------------
        if (ImGui::CollapsingHeader("Labels", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show labels (L)", &poiLayer.ShowLabels());
            ImGui::SameLine();
            ImGui::TextDisabled("%d / %d shown", poiLayer.VisibleCount(), poiLayer.GroundedCount());
            for (int i = 0; i < PoiCategory::kCount; ++i)
            {
                if (i % 3 != 0)
                {
                    ImGui::SameLine();
                }
                const glm::vec3 color = FGeoPoiLayer::CategoryColor(PoiCategory::kAll[static_cast<size_t>(i)]);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, 1.0f));
                ImGui::Checkbox(PoiCategory::kAll[static_cast<size_t>(i)], &poiLayer.CategoryEnabled(i));
                ImGui::PopStyleColor();
            }
        }

        // ---- Places -------------------------------------------------------
        if (tile != nullptr && ImGui::CollapsingHeader("Places", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const glm::vec3 from = context.walker != nullptr && context.walker->IsSpawned()
                                       ? context.walker->Position()
                                       : (context.cameraPosition != nullptr ? *context.cameraPosition
                                                                            : glm::vec3(0.0f));
            DrawPlaceList(*tile, poiLayer, from, request);
        }

        ImGui::End();
        return request;
    }

    void FGeoWalkUI::DrawPlaceList(const FGeoTile& tile, const FGeoPoiLayer& poiLayer,
                                   const glm::vec3& from, FGeoWalkUIRequest& request)
    {
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##filter", "filter by name", filter_, sizeof(filter_));
        ImGui::SameLine();
        ImGui::Checkbox("near", &sortByDistance_);

        struct FRow
        {
            int index;
            float distance;
        };
        std::vector<FRow> rows;
        rows.reserve(tile.pois.size());
        for (size_t i = 0; i < tile.pois.size(); ++i)
        {
            const FGeoPoi& poi = tile.pois[i];
            if (!poi.grounded || !poiLayer.IsCategoryEnabled(poi.category))
            {
                continue;
            }
            if (!ContainsNoCase(poi.name, filter_))
            {
                continue;
            }
            rows.push_back({static_cast<int>(i),
                            glm::distance(glm::vec2(from.x, from.z), poi.position)});
        }
        if (sortByDistance_)
        {
            std::sort(rows.begin(), rows.end(),
                      [](const FRow& a, const FRow& b) { return a.distance < b.distance; });
        }

        ImGui::TextDisabled("%zu place(s)", rows.size());
        if (ImGui::BeginChild("places", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const FRow& entry = rows[static_cast<size_t>(row)];
                    const FGeoPoi& poi = tile.pois[static_cast<size_t>(entry.index)];
                    const glm::vec3 color = FGeoPoiLayer::CategoryColor(poi.category);
                    ImGui::PushID(entry.index);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, 1.0f));
                    const bool selected = selectedPoi_ == entry.index;
                    if (ImGui::Selectable(poi.name.c_str(), selected))
                    {
                        selectedPoi_ = entry.index;
                        request.lookAtPoiIndex = entry.index;
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s\n%s · %.0f m away\ndouble-click to walk there",
                                          poi.name.c_str(), poi.tag.c_str(), entry.distance);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        request.walkToPoiIndex = entry.index;
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();

        if (selectedPoi_ >= 0 && selectedPoi_ < static_cast<int>(tile.pois.size()))
        {
            const FGeoPoi& poi = tile.pois[static_cast<size_t>(selectedPoi_)];
            ImGui::Separator();
            ImGui::TextWrapped("%s", poi.name.c_str());
            ImGui::TextDisabled("%s · %s · OSM %lld", poi.category.c_str(), poi.tag.c_str(),
                                static_cast<long long>(poi.osmId));
            if (poi.height > 0.0f)
            {
                ImGui::TextDisabled("height %.1f m, ground %.1f m", poi.height, poi.groundY);
            }
            else
            {
                ImGui::TextDisabled("ground %.1f m", poi.groundY);
            }
            if (ImGui::Button("Walk here"))
            {
                request.walkToPoiIndex = selectedPoi_;
            }
            ImGui::SameLine();
            if (ImGui::Button("Look at"))
            {
                request.lookAtPoiIndex = selectedPoi_;
            }
        }

        if (!tile.attribution.empty())
        {
            ImGui::Separator();
            for (const std::string& line : tile.attribution)
            {
                ImGui::TextDisabled("%s", line.c_str());
            }
        }
    }
}
