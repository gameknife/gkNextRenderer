#pragma once
#include <imgui.h>

#include "Core/EditorUiState.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <memory>
#include <string>

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

    Editor::EditorUiState& GetEditorUiState() { return uiState_; }
    const Editor::EditorUiState& GetEditorUiState() const { return uiState_; }

private:
    ImGuiID DockSpaceUI();
    void RebuildDefaultDockLayout(ImGuiID id);
    void ToolbarUI(EditorContext& ctx);
    void DrawIndicator(uint32_t frameCount);

    EditorGameInstance* editor_;
    Editor::EditorUiState uiState_{};
    std::string imguiIniPath_;

    bool firstRun_ = true;
};
