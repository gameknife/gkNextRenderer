#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Modules/NextDotNet/Interop.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <unordered_set>

// Native side of the binding surface. The table itself is expanded from EngineApi.def.h; what this
// header adds is the small amount of state the bindings need that has no home in the engine:
// which input was pressed this frame, whether a camera override is active, and the vectors a
// scene rebuild is currently filling.

namespace Modules::NextDotNet
{
    /// Per-frame input state. Kept here rather than queried from SDL so "pressed this frame" has
    /// the same clear-at-end-of-tick semantics scripts relied on under QuickJS.
    struct FInputState
    {
        std::unordered_set<SDL_Keycode> keysDown;
        std::unordered_set<SDL_Keycode> keysPressed;
        std::unordered_set<uint8_t> mouseButtonsDown;
        std::unordered_set<uint8_t> mouseButtonsPressed;
        std::unordered_set<uint8_t> gamepadButtonsDown;
        std::unordered_set<uint8_t> gamepadButtonsPressed;

        void ClearPressed()
        {
            keysPressed.clear();
            mouseButtonsPressed.clear();
            gamepadButtonsPressed.clear();
        }

        void Reset()
        {
            keysDown.clear();
            mouseButtonsDown.clear();
            gamepadButtonsDown.clear();
            ClearPressed();
        }
    };

    /// Valid only while the BeforeSceneRebuild hook is executing. SceneBuild bindings check
    /// IsValid() and refuse to run outside that window, preserving the QuickJS contract.
    struct FSceneBuildContext
    {
        std::vector<std::shared_ptr<Assets::Node>>* nodes = nullptr;
        std::vector<Assets::Model>* models = nullptr;
        std::vector<Assets::FMaterial>* materials = nullptr;
        std::vector<Assets::LightObject>* lights = nullptr;
        std::vector<Assets::AnimationTrack>* tracks = nullptr;

        bool IsValid() const { return nodes && models && materials && lights && tracks; }

        void Clear() { *this = FSceneBuildContext{}; }
    };

    extern FSceneBuildContext GSceneBuildContext;
    extern FInputState GInputState;

    /// Builds the table handed to managed code. Every def entry is filled; a binding declared
    /// without an implementation fails to compile.
    FEngineApi BuildEngineApi();
}
