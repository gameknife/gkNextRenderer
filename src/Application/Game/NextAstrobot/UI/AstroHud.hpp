#pragma once

// ============================================================================
// AstroHud.hpp - Immediate-mode HUD and screens (title, intro, pause, result),
// in the same style as Brotato3DUI: one context struct in, ImGui out, no state
// of its own beyond what the caller passes.
// ============================================================================

#include <string>

#include "Application/Game/NextAstrobot/Level/LevelFlow.hpp"

namespace NextAstrobot
{
    struct FHudContext
    {
        ELevelState state = ELevelState::Playing;
        const FRunStats* stats = nullptr;
        std::string levelName;
        std::string toast;
        float toastAlpha = 0.0f;
        float deathFade = 0.0f;
        bool showDebug = false;
        // Debug panel readouts.
        std::string locomotion;
        float playerX = 0.0f;
        float playerY = 0.0f;
        float playerZ = 0.0f;
        bool onGround = false;
        int checkpoint = -1;
        int mechanismCount = 0;
        int enemiesAlive = 0;
    };

    namespace AstroHud
    {
        /// Returns true when the UI consumed the frame's input.
        bool Draw(const FHudContext& context);
    }
}
