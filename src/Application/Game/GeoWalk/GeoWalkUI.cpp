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

        // A tab that reads as selected rather than as a button that happens to
        // be pressed; the view mode is the application's primary state.
        bool ViewTab(const char* label, bool active, const ImVec2& size)
        {
            const bool pushed = active;
            if (pushed)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.46f, 0.72f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.80f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.42f, 0.68f, 1.0f));
            }
            const bool clicked = ImGui::Button(label, size);
            if (pushed)
            {
                ImGui::PopStyleColor(3);
            }
            return clicked && !active;
        }
    }

    void FGeoWalkUI::DrawViewModeBar(const FGeoWalkUIContext& context, FGeoWalkUIRequest& request)
    {
        const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        const ImVec2 size(width, 0.0f);
        if (ViewTab("Walk (1)", context.viewMode == EViewMode::Walk, size))
        {
            request.setViewMode = true;
            request.viewMode = EViewMode::Walk;
        }
        ImGui::SameLine();
        if (ViewTab("Aerial (2)", context.viewMode == EViewMode::Aerial, size))
        {
            request.setViewMode = true;
            request.viewMode = EViewMode::Aerial;
        }
        ImGui::SameLine();
        if (ViewTab("Focus (3)", context.viewMode == EViewMode::Focus, size))
        {
            request.setViewMode = true;
            request.viewMode = EViewMode::Focus;
        }
    }

    void FGeoWalkUI::DrawWalkPanel(const FGeoWalkUIContext& context, FGeoWalkUIRequest& request)
    {
        if (context.walker == nullptr)
        {
            return;
        }
        const FGeoWalker& walker = *context.walker;
        ImGui::Text("Control: %s", ModeName(walker.Mode()));
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
        ImGui::TextDisabled("camera: %s (C)", context.followCamera ? "follow" : "free");
        ImGui::SameLine();
        if (ImGui::SmallButton("Respawn"))
        {
            request.respawn = true;
        }
    }

    void FGeoWalkUI::DrawAerialPanel(const FGeoWalkUIContext& context, const FGeoPoiLayer& poiLayer,
                                     FGeoWalkUIRequest& request)
    {
        ImGui::TextDisabled("right-drag orbit · WASD pan · wheel/QE zoom");
        ImGui::TextDisabled("click a marker to focus it");
        if (context.camera != nullptr)
        {
            const glm::vec2 pivot = context.camera->AerialPivot();
            ImGui::Text("Altitude %.0f m over %.0f, %.0f", context.camera->AerialDistance(),
                        pivot.x, pivot.y);
        }
        ImGui::Text("%d marker(s), %d named", poiLayer.MarkerCount(), poiLayer.PlacedCount());
        if (ImGui::Button("Start tour (T)"))
        {
            request.toggleTour = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Focus top place (G)"))
        {
            request.setViewMode = true;
            request.viewMode = EViewMode::Focus;
        }
    }

    void FGeoWalkUI::DrawFocusPanel(const FGeoWalkUIContext& context, const FGeoTile* tile,
                                    FGeoWalkUIRequest& request)
    {
        const FGeoPoi* poi = (tile != nullptr && context.focusPoi >= 0 &&
                              context.focusPoi < static_cast<int>(tile->pois.size()))
                                 ? &tile->pois[static_cast<size_t>(context.focusPoi)]
                                 : nullptr;
        if (poi == nullptr)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "no place selected");
            return;
        }

        const glm::vec3 color = FGeoPoiLayer::CategoryColor(poi->category);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, 1.0f));
        ImGui::TextWrapped("%s", poi->name.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s · %s", poi->category.c_str(), poi->tag.c_str());
        if (poi->height > 0.0f)
        {
            ImGui::TextDisabled("%.0f m tall · %.0f m² footprint", poi->height, poi->areaM2);
        }
        ImGui::TextDisabled("%d / %d in this browse order", context.focusOrdinal, context.focusTotal);

        if (ImGui::Button("< Prev (B)"))
        {
            request.focusPrev = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next (N) >"))
        {
            request.focusNext = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Walk here"))
        {
            request.walkToPoiIndex = context.focusPoi;
        }

        if (context.camera != nullptr)
        {
            ImGui::Checkbox("Auto-orbit (O)", &context.camera->AutoOrbit());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##orbitspeed", &context.camera->OrbitSpeed(), 0.0f,
                               Config::kFocusOrbitSpeedMax, "%.2f rad/s");
        }

        bool tour = context.tourActive;
        if (ImGui::Checkbox("Tour (T)", &tour))
        {
            request.toggleTour = true;
        }
        ImGui::SameLine();
        if (context.tourDwellSeconds != nullptr)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##dwell", context.tourDwellSeconds, Config::kTourDwellMin,
                               Config::kTourDwellMax, "%.0f s each");
        }
        if (context.tourActive)
        {
            ImGui::ProgressBar(context.tourProgress, ImVec2(-1.0f, 4.0f), "");
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
        ImGui::SetNextWindowSize(ImVec2(400.0f, 700.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("GeoWalk — tile browser", &visible_))
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

        // ---- View mode ----------------------------------------------------
        DrawViewModeBar(context, request);
        ImGui::Spacing();
        switch (context.viewMode)
        {
        case EViewMode::Aerial: DrawAerialPanel(context, poiLayer, request); break;
        case EViewMode::Focus: DrawFocusPanel(context, tile, request); break;
        case EViewMode::Walk:
        default: DrawWalkPanel(context, request); break;
        }
        ImGui::TextDisabled("%.1f ms/frame", context.frameMs);

        ImGui::Separator();

        // ---- Labels -------------------------------------------------------
        if (ImGui::CollapsingHeader("Labels", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show labels (L)", &poiLayer.ShowLabels());
            ImGui::SameLine();
            ImGui::TextDisabled("%d / %d shown", poiLayer.PlacedCount(), poiLayer.GroundedCount());
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
            DrawPlaceList(*tile, poiLayer, context, from, request);
        }

        ImGui::End();
        return request;
    }

    void FGeoWalkUI::DrawPlaceList(const FGeoTile& tile, const FGeoPoiLayer& poiLayer,
                                   const FGeoWalkUIContext& context, const glm::vec3& from,
                                   FGeoWalkUIRequest& request)
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
        if (ImGui::BeginChild("places", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders))
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
                        // Selecting a row does what the current view can do with
                        // it: from the air or an orbit that is a focus, from the
                        // street it is a glance in its direction.
                        if (context.viewMode == EViewMode::Walk)
                        {
                            request.lookAtPoiIndex = entry.index;
                        }
                        else
                        {
                            request.focusPoiIndex = entry.index;
                        }
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
            if (ImGui::Button("Focus"))
            {
                request.focusPoiIndex = selectedPoi_;
            }
            ImGui::SameLine();
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
