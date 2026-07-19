#pragma once

#include <array>

#include <SDL3/SDL_keycode.h>
#include <imgui.h>

#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Localization.hpp"

namespace Runtime
{
    namespace GraphicsDebugPanel
    {
        struct FRendererOption
        {
            const char* label;
            Vulkan::ERendererType type;
        };

        enum class EViewMode
        {
            Lit = 0,
            VisualDebug,
            Lighting,
            BoundingBox,
            PhysicsBodies,
            Edge,
            Skeleton,
            Wireframe,
            AreaLights,
            Custom,
        };

        inline constexpr std::array<FRendererOption, 5> RendererOptions = {{
            {"SoftwareTracing", Vulkan::ERT_SoftwareTracing},
            {"SoftwareModern", Vulkan::ERT_SoftwareModern},
            {"SoftwareModernNoAmbient", Vulkan::ERT_SoftwareModernNoAmbient},
            {"VoxelTracing", Vulkan::ERT_VoxelTracing},
            {"PathTracing", Vulkan::ERT_PathTracing},
        }};

        inline constexpr std::array<const char*, 9> ViewModeLabels = {{
            "Lit",
            "Visual Debug",
            "Lighting",
            "Bounding Box",
            "Physics Bodies",
            "Edge",
            "Skeleton",
            "Wireframe",
            "Area Lights",
        }};

        inline int GetRendererOptionCount(NextEngine& engine)
        {
            return engine.GetRenderer().SupportsRayTracing()
                ? static_cast<int>(RendererOptions.size())
                : static_cast<int>(RendererOptions.size()) - 1;
        }

        inline int ResolveRendererOptionIndex(const Runtime::Config::UserSettings& userSetting, int rendererOptionCount)
        {
            for (int index = 0; index < rendererOptionCount; ++index)
            {
                if (RendererOptions[index].type == static_cast<Vulkan::ERendererType>(userSetting.RendererType))
                {
                    return index;
                }
            }

            return -1;
        }

        inline void DrawRendererSelector(NextEngine& engine, Runtime::Config::UserSettings& userSetting, const char* comboId,
                                         float width = -1.0f)
        {
            const int rendererOptionCount = GetRendererOptionCount(engine);
            int currentRendererIndex = ResolveRendererOptionIndex(userSetting, rendererOptionCount);
            if (currentRendererIndex < 0)
            {
                currentRendererIndex = 0;
                userSetting.RendererType = static_cast<int32_t>(RendererOptions[currentRendererIndex].type);
            }

            ImGui::PushItemWidth(width);
            auto renderersGetter = [](void* data, int index) -> const char*
            {
                const auto* options = static_cast<const FRendererOption*>(data);
                return options[index].label;
            };
            if (ImGui::Combo(comboId,
                             &currentRendererIndex,
                             renderersGetter,
                             const_cast<FRendererOption*>(RendererOptions.data()),
                             rendererOptionCount))
            {
                userSetting.RendererType = static_cast<int32_t>(RendererOptions[currentRendererIndex].type);
            }
            ImGui::PopItemWidth();
        }

        inline const char* GetCurrentRendererLabel(NextEngine& engine, const Runtime::Config::UserSettings& userSetting)
        {
            const int rendererOptionCount = GetRendererOptionCount(engine);
            const int currentRendererIndex = ResolveRendererOptionIndex(userSetting, rendererOptionCount);
            if (currentRendererIndex >= 0)
            {
                return RendererOptions[currentRendererIndex].label;
            }

            return "Unknown";
        }

        inline const char* GetBoolStatusLabel(bool value)
        {
            return value ? LOCTEXT("On") : LOCTEXT("Off");
        }

        inline void DrawBadge(const char* text, const ImVec4& background, const ImVec4& foreground)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 padding(7.0f, 3.0f);
            const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

            drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(background), 8.0f);
            drawList->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), ImGui::GetColorU32(foreground), text);
            ImGui::Dummy(size);
        }

        inline void DrawSectionHeader(const char* title)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.42f, 1.0f), "%s", title);
            ImGui::Separator();
        }

        inline void DrawValueRow(const char* label, const char* value, const ImVec4& valueColor)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
            ImGui::SameLine(136.0f);
            ImGui::TextColored(valueColor, "%s", value);
        }

        inline void DrawBooleanRow(const char* label, bool enabled)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
            ImGui::SameLine(136.0f);
            DrawBadge(
                GetBoolStatusLabel(enabled),
                enabled ? ImVec4(0.14f, 0.36f, 0.20f, 0.92f) : ImVec4(0.22f, 0.24f, 0.28f, 0.88f),
                enabled ? ImVec4(0.86f, 1.0f, 0.90f, 1.0f) : ImVec4(0.85f, 0.88f, 0.92f, 0.96f));
        }

        inline void DrawShortcutRow(const char* shortcut, const char* label, bool active)
        {
            DrawBadge(
                shortcut,
                active ? ImVec4(0.22f, 0.32f, 0.52f, 0.95f) : ImVec4(0.20f, 0.22f, 0.27f, 0.90f),
                active ? ImVec4(0.92f, 0.97f, 1.0f, 1.0f) : ImVec4(0.82f, 0.85f, 0.90f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(active ? ImVec4(0.95f, 0.97f, 1.0f, 1.0f) : ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
        }

        inline void DrawAmbientCubeBrickStats(NextEngine& engine)
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
            DrawValueRow(LOCTEXT("AC Bricks"), brickStats.c_str(), ImVec4(0.70f, 0.94f, 1.0f, 1.0f));
        }

        inline bool CycleRenderer(NextEngine& engine)
        {
            Runtime::Config::UserSettings& userSetting = engine.GetUserSettings();
            const int rendererOptionCount = GetRendererOptionCount(engine);
            if (rendererOptionCount <= 0)
            {
                return false;
            }

            int currentRendererIndex = ResolveRendererOptionIndex(userSetting, rendererOptionCount);
            if (currentRendererIndex < 0)
            {
                currentRendererIndex = 0;
            }

            const int nextRendererIndex = (currentRendererIndex + 1) % rendererOptionCount;
            const auto nextRendererType = static_cast<int32_t>(RendererOptions[nextRendererIndex].type);
            if (userSetting.RendererType == nextRendererType)
            {
                return false;
            }

            userSetting.RendererType = nextRendererType;
            return true;
        }

        inline EViewMode ResolveViewMode(const Runtime::Config::ShowFlags& showFlags)
        {
            int enabledModeCount = 0;
            EViewMode activeMode = EViewMode::Lit;

            auto registerMode = [&](bool enabled, EViewMode mode)
            {
                if (!enabled)
                {
                    return;
                }

                ++enabledModeCount;
                activeMode = mode;
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

            if (enabledModeCount == 1)
            {
                return activeMode;
            }

            return EViewMode::Custom;
        }

        inline void ApplyViewMode(Runtime::Config::ShowFlags& showFlags, EViewMode mode)
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
            case EViewMode::VisualDebug:
                showFlags.ShowVisualDebug = true;
                break;
            case EViewMode::Lighting:
                showFlags.DebugDraw_Lighting = true;
                break;
            case EViewMode::BoundingBox:
                showFlags.DebugDraw_BoundingBox = true;
                break;
            case EViewMode::PhysicsBodies:
                showFlags.DebugDraw_PhysicsBodies = true;
                break;
            case EViewMode::Edge:
                showFlags.ShowEdge = true;
                break;
            case EViewMode::Skeleton:
                showFlags.ShowDebugSkeleton = true;
                break;
            case EViewMode::Wireframe:
                showFlags.ShowWireframe = true;
                break;
            case EViewMode::AreaLights:
                showFlags.DebugDraw_AreaLights = true;
                break;
            }
        }

        inline bool TryHandleViewModeShortcut(SDL_Keycode key, bool pressed, bool panelVisible, Runtime::Config::ShowFlags& showFlags)
        {
            if (!pressed || !panelVisible)
            {
                return false;
            }

            const EViewMode mode = [&]() -> EViewMode
            {
                switch (key)
                {
                case SDLK_1:
                case SDLK_KP_1:
                    return EViewMode::Lit;
                case SDLK_2:
                case SDLK_KP_2:
                    return EViewMode::VisualDebug;
                case SDLK_3:
                case SDLK_KP_3:
                    return EViewMode::Lighting;
                case SDLK_4:
                case SDLK_KP_4:
                    return EViewMode::BoundingBox;
                case SDLK_5:
                case SDLK_KP_5:
                    return EViewMode::PhysicsBodies;
                case SDLK_6:
                case SDLK_KP_6:
                    return EViewMode::Edge;
                case SDLK_7:
                case SDLK_KP_7:
                    return EViewMode::Skeleton;
                case SDLK_8:
                case SDLK_KP_8:
                    return EViewMode::Wireframe;
                case SDLK_9:
                case SDLK_KP_9:
                    return EViewMode::AreaLights;
                default:
                    return EViewMode::Custom;
                }
            }();

            if (mode == EViewMode::Custom)
            {
                return false;
            }

            ApplyViewMode(showFlags, mode);
            return true;
        }

        inline bool TryHandleRendererShortcut(SDL_Keycode key, bool pressed, bool panelVisible, NextEngine& engine)
        {
            if (!pressed || !panelVisible)
            {
                return false;
            }

            if (key != SDLK_Q)
            {
                return false;
            }

            return CycleRenderer(engine);
        }

        inline void DrawPanel(NextEngine& engine, bool& panelVisible, float topOffset)
        {
            if (!panelVisible)
            {
                return;
            }

            Runtime::Config::UserSettings& userSetting = engine.GetUserSettings();
            Runtime::Config::ShowFlags& showFlags = engine.GetShowFlags();
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float margin = 12.0f;
            const float rightReservedWidth = userSetting.ShowOverlay ? 400.0f : 0.0f;

            ImGui::SetNextWindowPos(
                ImVec2(viewport->Pos.x + viewport->Size.x - margin - rightReservedWidth,
                       viewport->Pos.y + topOffset + margin),
                ImGuiCond_Always,
                ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.88f);

            constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs;

            if (ImGui::Begin("Graphics Debug", nullptr, flags))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 7.0f));
                const EViewMode currentViewMode = ResolveViewMode(showFlags);
                const char* currentViewModeLabel = currentViewMode == EViewMode::Custom
                    ? LOCTEXT("Custom")
                    : LOCTEXT(ViewModeLabels[static_cast<size_t>(currentViewMode)]);

                DrawSectionHeader(LOCTEXT("Renderer"));
                DrawValueRow(LOCTEXT("Current"), GetCurrentRendererLabel(engine, userSetting), ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
                DrawAmbientCubeBrickStats(engine);
                ImGui::TextColored(ImVec4(0.52f, 0.57f, 0.65f, 1.0f), "%s", LOCTEXT("Available Renderers"));
                for (int index = 0; index < GetRendererOptionCount(engine); ++index)
                {
                    const bool isActive = RendererOptions[index].type == static_cast<Vulkan::ERendererType>(userSetting.RendererType);
                    DrawShortcutRow(isActive ? LOCTEXT("Active") : LOCTEXT("Inactive"), RendererOptions[index].label, isActive);
                }

                DrawSectionHeader(LOCTEXT("View Mode"));
                DrawValueRow(LOCTEXT("Current"), currentViewModeLabel, ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
                ImGui::TextColored(ImVec4(0.52f, 0.57f, 0.65f, 1.0f), "%s", LOCTEXT("Mode Shortcuts"));
                for (int index = 0; index < static_cast<int>(ViewModeLabels.size()); ++index)
                {
                    const auto mode = static_cast<EViewMode>(index);
                    const bool isActive = mode == currentViewMode;
                    const std::string shortcut = fmt::format("[{}]", index + 1);
                    DrawShortcutRow(shortcut.c_str(), LOCTEXT(ViewModeLabels[index]), isActive);
                }

                if (currentViewMode == EViewMode::Custom)
                {
                    ImGui::TextDisabled("%s", LOCTEXT("Custom View Mode Hint"));
                }

                DrawSectionHeader(LOCTEXT("Panel Shortcuts"));
                DrawShortcutRow("Q", LOCTEXT("Q: cycle renderer."), false);
                DrawShortcutRow("F2", LOCTEXT("F2: toggle Graphics Debug."), false);
                ImGui::PopStyleVar();
            }
            ImGui::End();
        }
    }
}
