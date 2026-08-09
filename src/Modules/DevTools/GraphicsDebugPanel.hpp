#pragma once

#include <array>

#include <SDL3/SDL_keycode.h>

#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Rendering/RendererChoices.hpp"

class NextEngine;

namespace Runtime
{
    namespace GraphicsDebugPanel
    {
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

        int GetRendererOptionCount(NextEngine& engine);
        const Rendering::FRendererChoice& GetRendererOption(NextEngine& engine, int index);
        int ResolveRendererOptionIndex(NextEngine& engine,
                                       const Runtime::Config::UserSettings& userSetting,
                                       int rendererOptionCount);
        void DrawRendererSelector(NextEngine& engine,
                                  Runtime::Config::UserSettings& userSetting,
                                  const char* comboId,
                                  float width = -1.0f);
        const char* GetCurrentRendererLabel(NextEngine& engine,
                                            const Runtime::Config::UserSettings& userSetting);
        bool CycleRenderer(NextEngine& engine);

        EViewMode ResolveViewMode(const Runtime::Config::ShowFlags& showFlags);
        void ApplyViewMode(Runtime::Config::ShowFlags& showFlags, EViewMode mode);
        bool TryHandleViewModeShortcut(SDL_Keycode key,
                                       bool pressed,
                                       bool panelVisible,
                                       Runtime::Config::ShowFlags& showFlags);
        bool TryHandleRendererShortcut(SDL_Keycode key,
                                       bool pressed,
                                       bool panelVisible,
                                       NextEngine& engine);
        void DrawPanel(NextEngine& engine, bool& panelVisible, float topOffset);
    }
}
