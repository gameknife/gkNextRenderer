#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Modules/NextDotNet/Interop.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <limits>
#include <unordered_set>

// Native side of the binding surface. The table itself is expanded from EngineApi.def.h; what this
// header adds is the small amount of state the bindings need that has no home in the engine:
// which input was pressed this frame, whether a camera override is active, and the vectors a
// scene rebuild is currently filling.

namespace Modules::NextDotNet
{
    /// Per-frame input state. Kept here rather than queried from SDL so gameplay Tick and its
    /// later UI hook can observe the same edge. The frame stamp also expires edges when UI is
    /// deliberately suppressed (for example, a screenshot without UI).
    struct FInputState
    {
        static constexpr uint32_t invalidPressedFrame = std::numeric_limits<uint32_t>::max();

        std::unordered_set<SDL_Keycode> keysDown;
        std::unordered_set<SDL_Keycode> keysPressed;
        std::unordered_set<uint8_t> mouseButtonsDown;
        std::unordered_set<uint8_t> mouseButtonsPressed;
        std::unordered_set<uint8_t> gamepadButtonsDown;
        std::unordered_set<uint8_t> gamepadButtonsPressed;
        std::array<int16_t, 6> gamepadAxes{};
        uint32_t pressedFrame = invalidPressedFrame;

        void ClearPressed()
        {
            keysPressed.clear();
            mouseButtonsPressed.clear();
            gamepadButtonsPressed.clear();
            pressedFrame = invalidPressedFrame;
        }

        void BeginPressedFrame(uint32_t frame)
        {
            if (pressedFrame != frame)
            {
                ClearPressed();
                pressedFrame = frame;
            }
        }

        void DiscardPressedBefore(uint32_t frame)
        {
            if (pressedFrame != invalidPressedFrame && pressedFrame != frame)
            {
                ClearPressed();
            }
        }

        void Reset()
        {
            keysDown.clear();
            mouseButtonsDown.clear();
            gamepadButtonsDown.clear();
            gamepadAxes.fill(0);
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

    /// The rectangle a hosted game's UI lives in, in ImGui screen coordinates.
    ///
    /// A game lays its HUD out against UI.GetScreenSize() and draws in absolute coordinates, which
    /// is correct when the game owns the window: the canvas is the whole ImGui main viewport, and
    /// that is what an inactive canvas (size 0) means. An editor running the game inside a docked
    /// viewport panel is the other case — without this the HUD lays out against the whole editor
    /// window and draws from its top-left corner, across the panels.
    ///
    /// Setting it makes the game see the panel as its screen: sizes reported to managed code are
    /// the panel's, drawn coordinates are offset into it, mouse position is reported relative to
    /// it, and drawing is clipped to it. No scaling — the game adapts to the panel the same way it
    /// adapts to a resized window, which keeps text crisp and avoids inventing a design resolution.
    struct FUiCanvas
    {
        FVec2 offset{0.0f, 0.0f};
        FVec2 size{0.0f, 0.0f};

        bool IsActive() const { return size.X > 0.0f && size.Y > 0.0f; }
        void Clear() { *this = FUiCanvas{}; }
    };

    extern FSceneBuildContext GSceneBuildContext;
    extern FInputState GInputState;
    extern FUiCanvas GUiCanvas;

    /// Builds the table handed to managed code. Every def entry is filled; a binding declared
    /// without an implementation fails to compile.
    FEngineApi BuildEngineApi();
}
