#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Character/CharacterActor.h"
#include "Gameplay/Components/AIAgentComponent.h"

namespace Assets
{
    struct Camera;
}

namespace CharacterDemoAIDebugUI
{
    struct FContext
    {
        bool showAIDebugMenu = false;
        bool showBehaviorTreeDebug = false;
        bool showNavGridDebug = false;
        bool aiEnabled = true;
        const NextGameplay::AIAgentComponent* agent = nullptr;
        const NextGameplay::CharacterActor* aiCharacter = nullptr;
        const NextGameplay::CharacterActor* playerCharacter = nullptr;
        const NextGameplay::FNavGrid* navGrid = nullptr;
        std::function<const char*()> getStateName;
        std::function<bool()> hasLineOfSightToPlayer;
        std::function<bool(Assets::Camera&)> overrideRenderCamera;
    };

    void DrawMenu(const FContext& context);
    void DrawBehaviorTreeOverlay(const FContext& context);
    void DrawNavGridOverlay(const FContext& context);
}
