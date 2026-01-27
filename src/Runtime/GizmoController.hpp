#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

class NextEngine;
struct ImGuiIO;

class GizmoController
{
public:
    void Draw(NextEngine& engine, const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    bool IsUsing() const { return isUsing_; }
    bool IsInteracting() const { return isUsing_ || isOver_; }
    bool IsShowing() const { return isShowing_; }

private:
    void DrawToolbar();
    void EnsureDefaults();
    void HandleShortcuts(const ImGuiIO& io);
    void ResetState();

    int operation_ = 0;
    int mode_ = 0;
    bool isUsing_ = false;
    bool isOver_ = false;
    bool isShowing_ = false;
    bool wasUsing_ = false;
};
