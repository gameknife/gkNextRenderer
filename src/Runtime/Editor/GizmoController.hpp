#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
    bool dragActive_ = false;
    uint32_t dragInstanceId_ = 0;
    glm::vec3 dragStartTranslation_ {};
    glm::quat dragStartRotation_ {};
    glm::vec3 dragStartScale_ {1.0f, 1.0f, 1.0f};
};
