#pragma once
#include <imgui.h>

#include "Application/Editor/gkNextEditor/Core/EditorUiState.hpp"
#include "Vulkan/DebugUtilities.hpp"

#include <memory>

struct EditorContext;

class EditorInterface final
{
public:
    VULKAN_NON_COPIABLE(EditorInterface)

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

    bool firstRun_ = true;
};
