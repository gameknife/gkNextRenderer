#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Modules/DevTools/Command/TransformNodesCommand.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct ImGuiIO;
struct ImGuiWindow;

namespace NextUI
{

class GizmoController
{
public:
    // viewportPos/viewportSize are ImGui logical screen coordinates. Convert
    // framebuffer-pixel rectangles with Scaling::MainFramebufferToImGuiViewport first.
    void Draw(NextEngine& engine, const glm::vec2& viewportPos, const glm::vec2& viewportSize,
              bool snapEnabled = false, float translateSnap = 1.0f, int defaultOperation = 0,
              const Assets::UniformBufferObject* viewUbo = nullptr, ImGuiWindow* alternativeWindow = nullptr);
    bool IsUsing() const { return isUsing_; }
    bool IsInteracting() const { return isUsing_ || isOver_; }
    bool IsShowing() const { return isShowing_; }
    int Operation() const { return operation_; }
    int Mode() const { return mode_; }

private:
    void DrawSelectionInfo(const std::string& objectName, const std::string& meshName,
                           const std::string& materialName);
    void DrawToolbar();
    void EnsureDefaults(int defaultOperation = 0);
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
    bool selectionMode_ = true;
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
