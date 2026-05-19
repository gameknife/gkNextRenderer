#pragma once
#include "Engine/Common/CoreMinimal.hpp"

class BrickPlayerGameInstance;

class BrickPlayerUserInterface
{
public:
    explicit BrickPlayerUserInterface(BrickPlayerGameInstance* gameInstance);
    void Render();
    void ApplyStyle();

private:
    void RenderTitleBar();
    void RenderTimeline();
    void RenderFreeBuildToolbar();
    void RenderWelcomeScreen();

    BrickPlayerGameInstance* gameInstance_;
};
