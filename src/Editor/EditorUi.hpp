#pragma once

#include "Common/CoreMinimal.hpp"

#include "Editor/Core/EditorUiState.hpp"
#include "Editor/EditorContext.hpp"

#include <imgui.h>

namespace Editor
{
    // Overlays (custom titlebar/toolbar/footer)
    void DrawTitleBarOverlay(EditorContext& ctx, EditorUiState& ui);

    // Docked panels
    void DrawOutlinerPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawPropertiesPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawContentBrowserPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawMaterialBrowserPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawTextureBrowserPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawMeshBrowserPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawCommandHistoryPanel(EditorContext& ctx, EditorUiState& ui);

    // Viewport overlay widgets (stats/tools)
    void DrawViewportOverlay(EditorContext& ctx, EditorUiState& ui);

    // Floating panels
    void DrawMaterialEditorPanel(EditorContext& ctx, EditorUiState& ui);

    // Cross-panel helpers
    void OpenMaterialEditor(EditorContext& ctx, EditorUiState& ui);
} // namespace Editor
