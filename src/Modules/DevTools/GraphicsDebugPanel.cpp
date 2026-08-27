#include "Engine/Common/CoreMinimal.hpp"

#include "GraphicsDebugPanel.hpp"

#include <imgui.h>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "UI/DiagnosticWidgets.hpp"

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
            DevToolsUI::DrawValueRow(
                LOCTEXT("AC Bricks"), brickStats.c_str(), ImVec4(0.70f, 0.94f, 1.0f, 1.0f));
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
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float margin = 12.0f;
        const float rightReservedWidth = userSetting.ShowOverlay ? 400.0f : 0.0f;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - margin - rightReservedWidth,
                   viewport->Pos.y + topOffset + margin),
            ImGuiCond_Always,
            ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.88f);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("Graphics Debug", nullptr, flags))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 7.0f));
            const EViewMode currentViewMode = ResolveViewMode(showFlags);
            const char* currentViewModeLabel = currentViewMode == EViewMode::Custom
                ? LOCTEXT("Custom")
                : LOCTEXT(ViewModeLabels[static_cast<size_t>(currentViewMode)]);

            DevToolsUI::DrawSectionHeader(LOCTEXT("Renderer"));
            DevToolsUI::DrawValueRow(
                LOCTEXT("Current"), GetCurrentRendererLabel(engine, userSetting), ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
            DrawAmbientCubeBrickStats(engine);
            ImGui::TextColored(ImVec4(0.52f, 0.57f, 0.65f, 1.0f), "%s", LOCTEXT("Available Renderers"));
            for (int index = 0; index < GetRendererOptionCount(engine); ++index)
            {
                const Rendering::FRendererChoice& option = GetRendererOption(engine, index);
                const bool isActive = option.type == static_cast<Vulkan::ERendererType>(userSetting.RendererType);
                DevToolsUI::DrawShortcutRow(
                    isActive ? LOCTEXT("Active") : LOCTEXT("Inactive"), option.displayName, isActive);
            }

            DevToolsUI::DrawSectionHeader(LOCTEXT("View Mode"));
            DevToolsUI::DrawValueRow(
                LOCTEXT("Current"), currentViewModeLabel, ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
            ImGui::TextColored(ImVec4(0.52f, 0.57f, 0.65f, 1.0f), "%s", LOCTEXT("Mode Shortcuts"));
            for (int index = 0; index < static_cast<int>(ViewModeLabels.size()); ++index)
            {
                const auto mode = static_cast<EViewMode>(index);
                const std::string shortcut = fmt::format("[{}]", index + 1);
                DevToolsUI::DrawShortcutRow(
                    shortcut.c_str(), LOCTEXT(ViewModeLabels[index]), mode == currentViewMode);
            }
            if (currentViewMode == EViewMode::Custom)
            {
                ImGui::TextDisabled("%s", LOCTEXT("Custom View Mode Hint"));
            }

            DevToolsUI::DrawSectionHeader(LOCTEXT("Panel Shortcuts"));
            DevToolsUI::DrawShortcutRow("Q", LOCTEXT("Q: cycle renderer."), false);
            DevToolsUI::DrawShortcutRow("F2", LOCTEXT("F2: toggle Graphics Debug."), false);
            ImGui::PopStyleVar();
        }
        ImGui::End();
    }
}
