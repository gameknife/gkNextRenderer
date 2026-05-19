#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Command/TransformNodesCommand.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

class NextEngine;
struct ImGuiIO;

namespace NextUI
{

class GizmoController
{
public:
    void Draw(NextEngine& engine, const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    bool IsUsing() const { return isUsing_; }
    bool IsInteracting() const { return isUsing_ || isOver_; }
    bool IsShowing() const { return isShowing_; }
    int Operation() const { return operation_; }
    int Mode() const { return mode_; }

private:
    void DrawToolbar();
    void EnsureDefaults();
    void HandleShortcuts(const ImGuiIO& io);
    void ResetState();

    enum class EGizmoPivotMode
    {
        Pivot = 0,
        SelectionBounds = 1
    };

    int operation_ = 0;
    int mode_ = 0;
    int pivotMode_ = static_cast<int>(EGizmoPivotMode::Pivot);
    bool isUsing_ = false;
    bool isOver_ = false;
    bool isShowing_ = false;
    bool wasUsing_ = false;
    bool multiSelectionModeInitialized_ = false;
    bool dragActive_ = false;
    std::vector<uint32_t> dragInstanceIds_;
    std::vector<glm::mat4> dragStartWorldMatrices_;
    std::vector<Runtime::Command::TransformSnapshot> dragStartSnapshots_;
    glm::mat4 dragStartGizmoMatrix_ {1.0f};
};

}
