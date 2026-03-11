#pragma once
#include "Common/CoreMinimal.hpp"

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
    void RenderWelcomeScreen();

    BrickPlayerGameInstance* gameInstance_;
};
