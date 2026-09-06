#include "Engine/Common/CoreMinimal.hpp"

#include "GraphicsDebugPanel.hpp"

#include <imgui.h>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "UI/DiagnosticWidgets.hpp"
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace Runtime::GraphicsDebugPanel
{
    namespace
    {
        Rendering::FRendererChoiceCapabilities GetCapabilities(NextEngine& engine)
        {
            return engine.GetRenderer().RendererChoiceCapabilities();
        }

        void DrawAmbientCubeBrickStats(NextEngine& engine)
        {
            if (!engine.GetRenderer().CurrentRendererRequirements().requestAmbientCube)
            {
                return;
            }

            const Assets::Scene& scene = engine.GetScene();
            uint32_t activeBricks = 0u;
            const uint32_t cascadeCapacity = scene.AmbientCubeCascadeCapacity();
            for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCapacity; ++cascadeIndex)
            {
                activeBricks += scene.AmbientActiveBrickCount(cascadeIndex);
            }

            const uint32_t totalBricks =
                cascadeCapacity * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
            const std::string brickStats = fmt::format("{} / {}", activeBricks, totalBricks);
            const ImVec4 colLabel = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
            const ImVec4 colVal = NextUI::Theme::Color(NextUI::Theme::EColor::Text);
            ImGui::TextColored(colLabel, "%s", LOCTEXT("AC Bricks"));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(colVal, "%s", brickStats.c_str());
        }
    }

    int GetRendererOptionCount(NextEngine& engine)
    {
        return static_cast<int>(Rendering::AvailableRendererChoices(GetCapabilities(engine)).size());
    }

    const Rendering::FRendererChoice& GetRendererOption(NextEngine& engine, const int index)
    {
        return *Rendering::AvailableRendererChoices(GetCapabilities(engine)).at(static_cast<size_t>(index));
    }

    int ResolveRendererOptionIndex(NextEngine& engine,
                                   const Runtime::Config::UserSettings& userSetting,
                                   const int rendererOptionCount)
    {
        for (int index = 0; index < rendererOptionCount; ++index)
        {
            if (GetRendererOption(engine, index).type ==
                static_cast<Vulkan::ERendererType>(userSetting.RendererType))
            {
                return index;
            }
        }
        return -1;
    }

    void DrawRendererSelector(NextEngine& engine,
                              Runtime::Config::UserSettings& userSetting,
                              const char* comboId,
                              const float width)
    {
        const int rendererOptionCount = GetRendererOptionCount(engine);
        if (rendererOptionCount <= 0)
        {
            // A device that cannot back the full bindless arrays runs the compatibility renderer
            // and has nothing to switch to. Report what is running instead of offering an empty
            // combo whose first entry does not exist.
            ImGui::TextDisabled("%s (no alternative on this GPU)",
                                Vulkan::GetRendererName(engine.GetRenderer().CurrentLogicRendererType()));
            return;
        }

        int currentRendererIndex = ResolveRendererOptionIndex(engine, userSetting, rendererOptionCount);
        if (currentRendererIndex < 0)
        {
            currentRendererIndex = 0;
            engine.RequestRendererType(GetRendererOption(engine, currentRendererIndex).type);
        }

        ImGui::PushItemWidth(width);
        const auto rendererGetter = [](void* data, const int index) -> const char*
        {
            return GetRendererOption(*static_cast<NextEngine*>(data), index).displayName;
        };
        if (ImGui::Combo(comboId, &currentRendererIndex, rendererGetter, &engine, rendererOptionCount))
        {
            engine.RequestRendererType(GetRendererOption(engine, currentRendererIndex).type);
        }
        ImGui::PopItemWidth();
    }

    const char* GetCurrentRendererLabel(NextEngine& engine,
                                        const Runtime::Config::UserSettings& userSetting)
    {
        const int rendererOptionCount = GetRendererOptionCount(engine);
        if (rendererOptionCount <= 0)
        {
            return Vulkan::GetRendererName(engine.GetRenderer().CurrentLogicRendererType());
        }
        const int currentRendererIndex = ResolveRendererOptionIndex(engine, userSetting, rendererOptionCount);
        return currentRendererIndex >= 0 ? GetRendererOption(engine, currentRendererIndex).displayName : "Unknown";
    }

    bool CycleRenderer(NextEngine& engine)
    {
        Runtime::Config::UserSettings& userSetting = engine.GetUserSettings();
        const int rendererOptionCount = GetRendererOptionCount(engine);
        if (rendererOptionCount <= 0)
        {
            return false;
        }

        int currentRendererIndex = ResolveRendererOptionIndex(engine, userSetting, rendererOptionCount);
        if (currentRendererIndex < 0)
        {
            currentRendererIndex = 0;
        }

        const auto nextRendererType = GetRendererOption(
            engine, (currentRendererIndex + 1) % rendererOptionCount).type;
        if (userSetting.RendererType == static_cast<int32_t>(nextRendererType))
        {
            return false;
        }
        return engine.RequestRendererType(nextRendererType);
    }

    EViewMode ResolveViewMode(const Runtime::Config::ShowFlags& showFlags)
    {
        int enabledModeCount = 0;
        EViewMode activeMode = EViewMode::Lit;
        const auto registerMode = [&](const bool enabled, const EViewMode mode)
        {
            if (enabled)
            {
                ++enabledModeCount;
                activeMode = mode;
            }
        };

        registerMode(showFlags.ShowVisualDebug, EViewMode::VisualDebug);
        registerMode(showFlags.DebugDraw_Lighting, EViewMode::Lighting);
        registerMode(showFlags.DebugDraw_BoundingBox, EViewMode::BoundingBox);
        registerMode(showFlags.DebugDraw_PhysicsBodies, EViewMode::PhysicsBodies);
        registerMode(showFlags.ShowEdge, EViewMode::Edge);
        registerMode(showFlags.ShowDebugSkeleton, EViewMode::Skeleton);
        registerMode(showFlags.ShowWireframe, EViewMode::Wireframe);
        registerMode(showFlags.DebugDraw_AreaLights, EViewMode::AreaLights);

        if (enabledModeCount == 0)
        {
            return EViewMode::Lit;
        }
        return enabledModeCount == 1 ? activeMode : EViewMode::Custom;
    }

    void ApplyViewMode(Runtime::Config::ShowFlags& showFlags, const EViewMode mode)
    {
        showFlags.ShowVisualDebug = false;
        showFlags.DebugDraw_Lighting = false;
        showFlags.DebugDraw_BoundingBox = false;
        showFlags.DebugDraw_PhysicsBodies = false;
        showFlags.ShowEdge = false;
        showFlags.ShowDebugSkeleton = false;
        showFlags.ShowWireframe = false;
        showFlags.DebugDraw_AreaLights = false;

        switch (mode)
        {
        case EViewMode::Lit:
        case EViewMode::Custom:
            break;
        case EViewMode::VisualDebug: showFlags.ShowVisualDebug = true; break;
        case EViewMode::Lighting: showFlags.DebugDraw_Lighting = true; break;
        case EViewMode::BoundingBox: showFlags.DebugDraw_BoundingBox = true; break;
        case EViewMode::PhysicsBodies: showFlags.DebugDraw_PhysicsBodies = true; break;
        case EViewMode::Edge: showFlags.ShowEdge = true; break;
        case EViewMode::Skeleton: showFlags.ShowDebugSkeleton = true; break;
        case EViewMode::Wireframe: showFlags.ShowWireframe = true; break;
        case EViewMode::AreaLights: showFlags.DebugDraw_AreaLights = true; break;
        }
    }

    bool TryHandleViewModeShortcut(const SDL_Keycode key,
                                   const bool pressed,
                                   const bool panelVisible,
                                   Runtime::Config::ShowFlags& showFlags)
    {
        if (!pressed || !panelVisible)
        {
            return false;
        }

        EViewMode mode = EViewMode::Custom;
        switch (key)
        {
        case SDLK_1: case SDLK_KP_1: mode = EViewMode::Lit; break;
        case SDLK_2: case SDLK_KP_2: mode = EViewMode::VisualDebug; break;
        case SDLK_3: case SDLK_KP_3: mode = EViewMode::Lighting; break;
        case SDLK_4: case SDLK_KP_4: mode = EViewMode::BoundingBox; break;
        case SDLK_5: case SDLK_KP_5: mode = EViewMode::PhysicsBodies; break;
        case SDLK_6: case SDLK_KP_6: mode = EViewMode::Edge; break;
        case SDLK_7: case SDLK_KP_7: mode = EViewMode::Skeleton; break;
        case SDLK_8: case SDLK_KP_8: mode = EViewMode::Wireframe; break;
        case SDLK_9: case SDLK_KP_9: mode = EViewMode::AreaLights; break;
        default: break;
        }

        if (mode == EViewMode::Custom)
        {
            return false;
        }
        ApplyViewMode(showFlags, mode);
        return true;
    }

    bool TryHandleRendererShortcut(const SDL_Keycode key,
                                   const bool pressed,
                                   const bool panelVisible,
                                   NextEngine& engine)
    {
        return pressed && panelVisible && key == SDLK_Q && CycleRenderer(engine);
    }

    void DrawPanel(NextEngine& engine, bool& panelVisible, const float topOffset)
    {
        if (!panelVisible)
        {
            return;
        }

        Runtime::Config::UserSettings& userSetting = engine.GetUserSettings();
        Runtime::Config::ShowFlags& showFlags = engine.GetShowFlags();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float distance = 12.0f;
        constexpr float panelWidth = 380.0f;

        const float rightOffset = distance + (userSetting.ShowOverlay ? 380.0f + 10.0f : 0.0f);
        const ImVec2 pos = ImVec2(viewport->Pos.x + viewport->Size.x - rightOffset - panelWidth,
                                  viewport->Pos.y + distance + (topOffset > 0.0f ? topOffset : 44.0f));
        const float panelHeight = std::max(420.0f, viewport->Size.y - distance - 86.0f);

        NextUI::Theme::FDetailPanelConfig panelConfig{};
        panelConfig.WindowId = "##GraphicsDebugPanel";
        panelConfig.ContentWindowId = "##GraphicsDebugContent";
        panelConfig.Icon = ICON_FA_MICROCHIP;
        panelConfig.Title = "Graphics Debug";
        panelConfig.Open = &panelVisible;
        panelConfig.Position = pos;
        panelConfig.Size = ImVec2(panelWidth, panelHeight);

        if (!NextUI::Theme::BeginDetailPanel(panelConfig))
        {
            return;
        }

        constexpr float cardHorizontalInset = 4.0f;
        auto BeginCard = [&](const char* id, float height = 0.0f, ImGuiWindowFlags extraFlags = 0)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const float cardWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - cardHorizontalInset * 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardHorizontalInset);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.38f));
            ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.84f));
            if (height > 0.0f)
            {
                ImGui::BeginChild(id, ImVec2(cardWidth, height), ImGuiChildFlags_Borders, extraFlags);
            }
            else
            {
                ImGui::BeginChild(id, ImVec2(cardWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, extraFlags);
            }
        };

        auto EndCard = [&]()
        {
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        };

        const ImVec4 colHeader = NextUI::Theme::Color(NextUI::Theme::EColor::Blue);
        const ImVec4 colLabel = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
        const ImVec4 colVal = NextUI::Theme::Color(NextUI::Theme::EColor::Text);
        const ImVec4 colWarn = NextUI::Theme::Color(NextUI::Theme::EColor::Warning);

        auto CompactStat = [&](const char* label, const std::string& value)
        {
            ImGui::TextColored(colLabel, "%s", label);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(colVal, "%s", value.c_str());
        };

        const EViewMode currentViewMode = ResolveViewMode(showFlags);
        const std::string currentViewModeLabel = currentViewMode == EViewMode::Custom
            ? std::string(LOCTEXT("Custom"))
            : std::string(LOCTEXT(ViewModeLabels[static_cast<size_t>(currentViewMode)]));

        // 1. Renderer Pipeline Card
        BeginCard("##GraphicsRendererCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("Renderer Pipeline"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        const std::string currentRendererLabel = GetCurrentRendererLabel(engine, userSetting);
        CompactStat(LOCTEXT("Current"), currentRendererLabel);
        DrawAmbientCubeBrickStats(engine);

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextColored(colLabel, "%s", LOCTEXT("Available Renderers"));
        const int rendererOptionCount = GetRendererOptionCount(engine);
        if (rendererOptionCount <= 0)
        {
            ImGui::TextDisabled("%s (%s)",
                                Vulkan::GetRendererName(engine.GetRenderer().CurrentLogicRendererType()),
                                LOCTEXT("no alternative on this GPU"));
        }
        else
        {
            for (int index = 0; index < rendererOptionCount; ++index)
            {
                const Rendering::FRendererChoice& option = GetRendererOption(engine, index);
                const bool isActive = option.type == static_cast<Vulkan::ERendererType>(userSetting.RendererType);
                ImGui::PushID(index);
                if (isActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.45f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover, 0.65f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover, 0.85f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.25f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.50f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.80f));
                }
                const std::string btnText = fmt::format("{}  {}{}",
                    isActive ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE,
                    option.displayName,
                    isActive ? "  (Active)" : "");
                if (ImGui::Button(btnText.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26.0f)))
                {
                    engine.RequestRendererType(option.type);
                }
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }
        EndCard();

        // 2. View Mode Card
        BeginCard("##GraphicsViewModeCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("View Mode"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        CompactStat(LOCTEXT("Current Mode"), currentViewModeLabel);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("##ViewModesTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            for (int index = 0; index < static_cast<int>(ViewModeLabels.size()); ++index)
            {
                ImGui::TableNextColumn();
                const auto mode = static_cast<EViewMode>(index);
                const bool isActive = (mode == currentViewMode);
                ImGui::PushID(index);
                if (isActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.45f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover, 0.65f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover, 0.85f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.25f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.50f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.80f));
                }
                const std::string label = fmt::format("[{}] {}", index + 1, LOCTEXT(ViewModeLabels[index]));
                if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26.0f)))
                {
                    ApplyViewMode(showFlags, mode);
                }
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (currentViewMode == EViewMode::Custom)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::TextColored(colWarn, "%s", LOCTEXT("Custom View Mode Hint"));
        }
        EndCard();

        // 3. Shortcuts Card
        BeginCard("##GraphicsShortcutsCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("Shortcuts"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::BeginTable("##GraphicsShortcutsTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Desc", ImGuiTableColumnFlags_WidthStretch);

            auto ShortcutRow = [&](const char* key, const char* desc)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(colHeader, "%s", key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(colLabel, "%s", desc);
            };

            ShortcutRow("Q", LOCTEXT("Cycle renderer"));
            ShortcutRow("1 ~ 9", LOCTEXT("Switch view mode"));
            ShortcutRow("F1", LOCTEXT("Physics Debug Overlay"));
            ShortcutRow("F2", LOCTEXT("Graphics Debug Panel"));
            ShortcutRow("F3", LOCTEXT("Statistics Overlay"));
            ImGui::EndTable();
        }
        EndCard();

        NextUI::Theme::EndDetailPanel();
    }
}
