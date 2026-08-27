#pragma once
#include <imgui.h>

#include "Core/EditorUiState.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <unordered_map>

struct EditorContext;

class EditorInterface final
{
public:
    GK_NON_COPIABLE(EditorInterface)

    EditorInterface(class EditorGameInstance* editor);
    ~EditorInterface();

    void Config();
    void Init();
    void Render();
    void Render(const NextGameInstanceBase::FGameUiFrameContext& context);
    void OnRemoteUiSessionClosed(std::string_view sessionId);

    Editor::EditorUiState& GetEditorUiState() { return uiState_; }
    const Editor::EditorUiState& GetEditorUiState() const { return uiState_; }

private:
    Editor::EditorUiState& GetRemoteUiState(std::string_view sessionId);
    void Render(Editor::EditorUiState& uiState);
    ImGuiID DockSpaceUI(Editor::EditorUiState& uiState);
    void RebuildDefaultDockLayout(ImGuiID id);
    void ToolbarUI(EditorContext& ctx, Editor::EditorUiState& uiState);

    EditorGameInstance* editor_;
    Editor::EditorUiState uiState_{};
    std::unordered_map<std::string, Editor::EditorUiState> remoteUiStates_;
    std::string imguiIniPath_;
    // Last scene rect handed to the renderer, in framebuffer pixels. Kept here because the
    // swapchain's output viewport now lags a resize by the frames it takes to rebuild.
    VkRect2D sceneViewportRect_{};

    bool firstRun_ = true;
};
