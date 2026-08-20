#include "NextWorldTravelUI.h"

#include "NextWorldTravelConfig.hpp"
#include "NextWorldTraveler.h"

#include "Modules/NextUI/UI/DesktopUI.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <cstring>

namespace NextWorldTravel
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

    void FNextWorldTravelUI::DrawViewportToolbar(const FNextWorldTravelUIContext& context,
                                                  FGeoPoiLayer& poiLayer,
                                                  FNextWorldTravelUIRequest& request)
    {
        if (context.tiles == nullptr)
        {
            return;
        }

        const std::vector<FGeoTile>& tiles = *context.tiles;
        const FGeoTile* tile = context.activeTile >= 0 && context.activeTile < static_cast<int>(tiles.size())
            ? &tiles[static_cast<size_t>(context.activeTile)]
            : nullptr;
        const char* tileLabel = tile != nullptr ? tile->name.c_str() : "No tile";
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float margin = 12.0f;
        const float toolbarHeight = std::ceil(ImGui::GetFontSize() + 22.0f);
        const float tileWidth = std::clamp(ImGui::CalcTextSize(tileLabel).x + 54.0f, 150.0f, 250.0f);

        NextUI::Theme::FOverlayPanelConfig navigationConfig{};
        navigationConfig.WindowId = "##NextWorldTravelNavigation";
        navigationConfig.Position = viewport->Pos + ImVec2(margin, margin);
        navigationConfig.Size = ImVec2(tileWidth + 160.0f, toolbarHeight);
        navigationConfig.Padding = ImVec2(5.0f, 7.0f);
        navigationConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
        navigationConfig.Rounding = 5.0f;
        navigationConfig.BackgroundAlpha = 0.80f;
        if (NextUI::Theme::BeginOverlayPanel(navigationConfig))
        {
            NextUI::Theme::PushViewportToolbarStyle();
            ImGui::SetNextItemWidth(tileWidth);
            NextUI::Theme::PushViewportPopupStyle();
            if (ImGui::BeginCombo("##TravelTile", tileLabel))
            {
                for (size_t i = 0; i < tiles.size(); ++i)
                {
                    const bool selected = static_cast<int>(i) == context.activeTile;
                    if (NextUI::Theme::DrawViewportComboOption(tiles[i].name.c_str(), selected) && !selected)
                    {
                        request.loadTileIndex = static_cast<int>(i);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            NextUI::Theme::PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Choose a generated world tile");
            NextUI::Theme::DrawVerticalSeparator(ImGui::GetFrameHeight(), 7.0f);

            const auto selectView = [&](const char* label, const char* tooltip, EViewMode mode)
            {
                if (NextUI::Theme::DrawFlatViewportButton(
                        label, tooltip, context.viewMode == mode, ImVec2(28.0f, 22.0f)) &&
                    context.viewMode != mode)
                {
                    request.setViewMode = true;
                    request.viewMode = mode;
                }
                ImGui::SameLine();
            };
            selectView(ICON_FA_PERSON_WALKING "##Walk", "Walk view (1)", EViewMode::Walk);
            selectView(ICON_FA_MAP "##Aerial", "Aerial map view (2 or V)", EViewMode::Aerial);
            selectView(ICON_FA_LOCATION_CROSSHAIRS "##Focus", "Focus the current place (3 or G)", EViewMode::Focus);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            NextUI::Theme::PopViewportToolbarStyle();
        }
        NextUI::Theme::EndOverlayPanel();

        constexpr float actionWidth = 250.0f;
        NextUI::Theme::FOverlayPanelConfig actionConfig{};
        actionConfig.WindowId = "##NextWorldTravelViewportTools";
        actionConfig.Position = ImVec2(viewport->Pos.x + viewport->Size.x - margin - actionWidth,
                                       viewport->Pos.y + margin);
        actionConfig.Size = ImVec2(actionWidth, toolbarHeight);
        actionConfig.Padding = ImVec2(5.0f, 7.0f);
        actionConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
        actionConfig.Rounding = 5.0f;
        actionConfig.BackgroundAlpha = 0.80f;
        if (NextUI::Theme::BeginOverlayPanel(actionConfig))
        {
            NextUI::Theme::PushViewportToolbarStyle();
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_TAGS "##Labels", "Show or hide place labels (L)", poiLayer.ShowLabels(),
                    ImVec2(28.0f, 22.0f)))
            {
                poiLayer.ShowLabels() = !poiLayer.ShowLabels();
            }
            ImGui::SameLine();

            const bool canToggleFollow = context.camera != nullptr && context.viewMode == EViewMode::Walk;
            ImGui::BeginDisabled(!canToggleFollow);
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_COMPASS "##Follow", "Switch between follow and free camera (C)",
                    context.followCamera, ImVec2(28.0f, 22.0f)))
            {
                context.camera->ToggleWalkCamera();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();

            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_ARROWS_ROTATE "##Tour", "Start or stop the landmark tour (T)", context.tourActive,
                    ImVec2(28.0f, 22.0f)))
            {
                request.toggleTour = true;
            }
            ImGui::SameLine();

            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_ARROWS_ROTATE "##Reset", "Reset the current viewport", false, ImVec2(28.0f, 22.0f)))
            {
                request.resetViewport = true;
            }
            ImGui::SameLine();
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_CAMERA "##Capture", "Capture a screenshot (F9)", false, ImVec2(28.0f, 22.0f)))
            {
                request.takeScreenshot = true;
            }
            ImGui::SameLine();
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_SLIDERS "##Explorer", "Show or hide the world explorer", showExplorer_,
                    ImVec2(28.0f, 22.0f)))
            {
                showExplorer_ = !showExplorer_;
            }
            ImGui::SameLine();
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_KEYBOARD "##Shortcuts", "Show viewport shortcuts", showShortcutSheet_,
                    ImVec2(28.0f, 22.0f)))
            {
                showShortcutSheet_ = !showShortcutSheet_;
            }
            NextUI::Theme::PopViewportToolbarStyle();
        }
        NextUI::Theme::EndOverlayPanel();
    }

    void FNextWorldTravelUI::DrawShortcutSheet()
    {
        if (!showShortcutSheet_)
        {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float margin = 12.0f;
        constexpr float panelWidth = 370.0f;
        const float toolbarHeight = std::ceil(ImGui::GetFontSize() + 22.0f);
        NextUI::Theme::FOverlayPanelConfig config{};
        config.WindowId = "##NextWorldTravelShortcuts";
        config.Position = ImVec2(viewport->Pos.x + viewport->Size.x - margin - panelWidth,
                                 viewport->Pos.y + margin + toolbarHeight + 8.0f);
        config.Size = ImVec2(panelWidth, 290.0f);
        config.Padding = ImVec2(14.0f, 10.0f);
        config.ItemSpacing = ImVec2(6.0f, 5.0f);
        config.Rounding = 6.0f;
        config.BackgroundAlpha = 0.92f;
        if (NextUI::Theme::BeginOverlayPanel(config))
        {
            ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Text),
                               "%s  Viewport shortcuts", ICON_FA_KEYBOARD);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 24.0f);
            NextUI::Theme::PushViewportToolbarStyle();
            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_XMARK "##CloseShortcuts", "Close shortcuts", false, ImVec2(24.0f, 22.0f)))
            {
                showShortcutSheet_ = false;
            }
            NextUI::Theme::PopViewportToolbarStyle();
            NextUI::Theme::DrawThinSeparator();

            struct FShortcut
            {
                const char* key;
                const char* action;
            };
            constexpr FShortcut shortcuts[] = {
                {"1 / 2 / 3", "Walk, aerial map, focus"},
                {"V / G", "Aerial map / focus top place"},
                {"C", "Follow or free camera in Walk"},
                {"T / N / B", "Tour, next place, previous place"},
                {"L / O", "Labels / auto-orbit"},
                {"RMB + drag", "Look or orbit"},
                {"Wheel", "Zoom"},
                {"F9", "Screenshot"},
                {"Tab", "Hide all travel UI"},
            };
            if (ImGui::BeginTable("##TravelShortcuts", 2,
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 108.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                for (const FShortcut& shortcut : shortcuts)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Text), "%s", shortcut.key);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", shortcut.action);
                }
                ImGui::EndTable();
            }
        }
        NextUI::Theme::EndOverlayPanel();
    }

    void FNextWorldTravelUI::DrawWalkPanel(const FNextWorldTravelUIContext& context, FNextWorldTravelUIRequest& request)
    {
        if (context.walker == nullptr)
        {
            return;
        }
        const FNextWorldTraveler& walker = *context.walker;
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

    void FNextWorldTravelUI::DrawAerialPanel(const FNextWorldTravelUIContext& context, const FGeoPoiLayer& poiLayer,
                                     FNextWorldTravelUIRequest& request)
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

    void FNextWorldTravelUI::DrawFocusPanel(const FNextWorldTravelUIContext& context, const FGeoTile* tile,
                                    FNextWorldTravelUIRequest& request)
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

    FNextWorldTravelUIRequest FNextWorldTravelUI::Draw(const FNextWorldTravelUIContext& context, FGeoPoiLayer& poiLayer)
    {
        FNextWorldTravelUIRequest request;
        if (!visible_ || context.tiles == nullptr)
        {
            return request;
        }

        DrawViewportToolbar(context, poiLayer, request);
        DrawShortcutSheet();
        if (!showExplorer_)
        {
            return request;
        }

        const std::vector<FGeoTile>& tiles = *context.tiles;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        NextUI::Theme::FDetailPanelConfig panelConfig{};
        panelConfig.WindowId = "##NextWorldTravelExplorer";
        panelConfig.ContentWindowId = "##NextWorldTravelExplorerContent";
        panelConfig.Icon = ICON_FA_MAP_LOCATION_DOT;
        panelConfig.Title = "World explorer";
        panelConfig.Open = &showExplorer_;
        panelConfig.Position = viewport->Pos + ImVec2(16.0f, std::ceil(ImGui::GetFontSize() + 42.0f));
        panelConfig.Size = ImVec2(390.0f, 690.0f);
        if (!NextUI::Theme::BeginDetailPanel(panelConfig))
        {
            return request;
        }

        // ---- Tile ---------------------------------------------------------
        const FGeoTile* tile = (context.activeTile >= 0 &&
                                context.activeTile < static_cast<int>(tiles.size()))
                                   ? &tiles[static_cast<size_t>(context.activeTile)]
                                   : nullptr;
        if (tile != nullptr)
        {
            ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted),
                                "%.5f, %.5f  ·  %.0f m  ·  %zu places",
                                tile->center.x, tile->center.y, tile->sizeM, tile->pois.size());
            if (!tile->loadError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tile->loadError.c_str());
            }
        }

        NextUI::Theme::DrawThinSeparator();
        switch (context.viewMode)
        {
        case EViewMode::Aerial: DrawAerialPanel(context, poiLayer, request); break;
        case EViewMode::Focus: DrawFocusPanel(context, tile, request); break;
        case EViewMode::Walk:
        default: DrawWalkPanel(context, request); break;
        }
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted),
                           "%.1f ms/frame", context.frameMs);
        NextUI::Theme::DrawThinSeparator();

        // ---- Labels -------------------------------------------------------
        if (NextUI::Theme::BeginPanelSection("Labels", true))
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
            NextUI::Theme::EndPanelSection();
        }

        // ---- Places -------------------------------------------------------
        if (tile != nullptr && NextUI::Theme::BeginPanelSection("Places", true))
        {
            const glm::vec3 from = context.walker != nullptr && context.walker->IsSpawned()
                                       ? context.walker->Position()
                                       : (context.cameraPosition != nullptr ? *context.cameraPosition
                                                                            : glm::vec3(0.0f));
            DrawPlaceList(*tile, poiLayer, context, from, request);
            NextUI::Theme::EndPanelSection();
        }

        NextUI::Theme::EndDetailPanel();
        return request;
    }

    void FNextWorldTravelUI::DrawPlaceList(const FGeoTile& tile, const FGeoPoiLayer& poiLayer,
                                   const FNextWorldTravelUIContext& context, const glm::vec3& from,
                                   FNextWorldTravelUIRequest& request)
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
