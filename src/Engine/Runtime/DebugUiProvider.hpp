#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <SDL3/SDL_keycode.h>

namespace Runtime
{
    // Hook interface for developer debug UI (overlays, panels and their
    // keyboard shortcuts). The engine core only owns this interface; the
    // implementation lives in Modules/DevTools and is registered by the
    // application entry point via NextEngine::SetDebugUiProvider.
    class IDebugUiProvider
    {
    public:
        virtual ~IDebugUiProvider() = default;

        virtual void DrawPhysicsOverlay(const Assets::Scene& scene, const Assets::Camera& camera) = 0;
        virtual void DrawGraphicsPanel(NextEngine& engine, bool& panelVisible, float topOffset) = 0;
        virtual void DrawProfileOverlay(NextEngine& engine, const NextUI::Statistics& statistics,
                                        VulkanGpuTimer* gpuTimer, float topOffset) = 0;
        virtual bool HandleRendererShortcut(SDL_Keycode key, bool pressed, bool panelVisible, NextEngine& engine) = 0;
        virtual bool HandleViewModeShortcut(SDL_Keycode key, bool pressed, bool panelVisible,
                                            Runtime::Config::ShowFlags& showFlags) = 0;
    };
}
