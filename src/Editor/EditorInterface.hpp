#pragma once
#include <imgui.h>

#include "Editor/Core/EditorUiState.hpp"
#include "Vulkan/DebugUtilities.hpp"

#include <memory>

class EditorInterface final
{
public:
    VULKAN_NON_COPIABLE(EditorInterface)

    EditorInterface(class EditorGameInstance* editor);
    ~EditorInterface();

    void Config();
    void Init();
    void Render();

private:
    ImGuiID DockSpaceUI();
    void RebuildDefaultDockLayout(ImGuiID id);
    void ToolbarUI();
    void DrawIndicator(uint32_t frameCount);

    EditorGameInstance* editor_;
    Editor::EditorUiState uiState_{};

    bool firstRun_ = true;
};
