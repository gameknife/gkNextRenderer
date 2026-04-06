#pragma once

#include <array>

#include <SDL3/SDL_keycode.h>
#include <imgui.h>

#include "Runtime/Config/ShowFlags.hpp"
#include "Runtime/Config/UserSettings.hpp"
#include "Runtime/Engine.hpp"
#include "Utilities/Localization.hpp"

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
            Custom,
        };

        inline constexpr std::array<FRendererOption, 4> RendererOptions = {{
            {"SoftTracing", Vulkan::ERT_ModernDeferred},
            {"SoftModern", Vulkan::ERT_LegacyDeferred},
            {"VoxelTracing", Vulkan::ERT_VoxelTracing},
            {"PathTracing", Vulkan::ERT_PathTracing},
        }};

        inline constexpr std::array<const char*, 8> ViewModeLabels = {{
            "Lit",
            "Visual Debug",
            "Lighting",
            "Bounding Box",
            "Physics Bodies",
            "Edge",
            "Skeleton",
            "Wireframe",
        }};

        inline int GetRendererOptionCount(NextEngine& engine)
        {
            return engine.GetRenderer().SupportsRayTracing()
                ? static_cast<int>(RendererOptions.size())
                : static_cast<int>(RendererOptions.size()) - 1;
        }

        inline int ResolveRendererOptionIndex(const UserSettings& userSetting, int rendererOptionCount)
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

        inline void DrawRendererSelector(NextEngine& engine, UserSettings& userSetting, const char* comboId)
        {
            const int rendererOptionCount = GetRendererOptionCount(engine);
            int currentRendererIndex = ResolveRendererOptionIndex(userSetting, rendererOptionCount);
            if (currentRendererIndex < 0)
            {
                currentRendererIndex = 0;
                userSetting.RendererType = static_cast<int32_t>(RendererOptions[currentRendererIndex].type);
            }

            ImGui::PushItemWidth(-1);
            auto renderersGetter = [](void* data, int index, const char** outText)
            {
                const auto* options = static_cast<const FRendererOption*>(data);
                *outText = options[index].label;
                return true;
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

        inline EViewMode ResolveViewMode(const ShowFlags& showFlags)
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

        inline void ApplyViewMode(ShowFlags& showFlags, EViewMode mode)
        {
            showFlags.ShowVisualDebug = false;
            showFlags.DebugDraw_Lighting = false;
            showFlags.DebugDraw_BoundingBox = false;
            showFlags.DebugDraw_PhysicsBodies = false;
            showFlags.ShowEdge = false;
            showFlags.ShowDebugSkeleton = false;
            showFlags.ShowWireframe = false;

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
            }
        }

        inline bool TryHandleViewModeShortcut(SDL_Keycode key, bool pressed, bool panelVisible, ShowFlags& showFlags)
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

        inline void DrawPanel(NextEngine& engine, bool& panelVisible, float topOffset)
        {
            if (!panelVisible)
            {
                return;
            }

            UserSettings& userSetting = engine.GetUserSettings();
            ShowFlags& showFlags = engine.GetShowFlags();
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float margin = 12.0f;

            ImGui::SetNextWindowPos(
                ImVec2(viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + topOffset + margin),
                ImGuiCond_Always,
                ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.82f);

            constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::Begin("Graphics Debug", &panelVisible, flags))
            {
                const EViewMode currentViewMode = ResolveViewMode(showFlags);

                ImGui::TextUnformatted(LOCTEXT("Renderer"));
                DrawRendererSelector(engine, userSetting, "##GraphicsDebugRendererList");

                ImGui::Spacing();
                ImGui::TextUnformatted(LOCTEXT("View Mode"));
                ImGui::PushItemWidth(-1);
                const char* currentLabel = currentViewMode == EViewMode::Custom
                    ? LOCTEXT("Custom")
                    : LOCTEXT(ViewModeLabels[static_cast<size_t>(currentViewMode)]);
                if (ImGui::BeginCombo("##GraphicsDebugViewMode", currentLabel))
                {
                    for (int index = 0; index < static_cast<int>(ViewModeLabels.size()); ++index)
                    {
                        const auto mode = static_cast<EViewMode>(index);
                        const bool isSelected = mode == currentViewMode;
                        if (ImGui::Selectable(LOCTEXT(ViewModeLabels[index]), isSelected))
                        {
                            ApplyViewMode(showFlags, mode);
                        }

                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    if (currentViewMode == EViewMode::Custom)
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", LOCTEXT("Custom"));
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::Separator();
                ImGui::TextUnformatted(LOCTEXT("Mode Shortcuts"));
                for (int index = 0; index < static_cast<int>(ViewModeLabels.size()); ++index)
                {
                    const auto mode = static_cast<EViewMode>(index);
                    const bool isActive = mode == currentViewMode;
                    if (ImGui::Selectable(fmt::format("[{}] {}", index + 1, LOCTEXT(ViewModeLabels[index])).c_str(), isActive))
                    {
                        ApplyViewMode(showFlags, mode);
                    }
                }

                if (currentViewMode == EViewMode::Custom)
                {
                    ImGui::TextDisabled("%s", LOCTEXT("Custom View Mode Hint"));
                }

                ImGui::Separator();
                ImGui::Checkbox(LOCTEXT("Show Grid"), &showFlags.ShowGrid);
                ImGui::Checkbox(LOCTEXT("Visual Debug"), &showFlags.ShowVisualDebug);
                ImGui::Checkbox(LOCTEXT("Lighting"), &showFlags.DebugDraw_Lighting);
                ImGui::Checkbox(LOCTEXT("Bounding Box"), &showFlags.DebugDraw_BoundingBox);
                ImGui::Checkbox(LOCTEXT("Physics Bodies"), &showFlags.DebugDraw_PhysicsBodies);
                ImGui::Checkbox(LOCTEXT("Edge"), &showFlags.ShowEdge);
                ImGui::Checkbox(LOCTEXT("Skeleton"), &showFlags.ShowDebugSkeleton);
                ImGui::Checkbox(LOCTEXT("Wireframe"), &showFlags.ShowWireframe);
                ImGui::Separator();
                ImGui::TextDisabled("%s", LOCTEXT("F2: toggle Graphics Debug."));
            }
            ImGui::End();
        }
    }
}
