#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include "Core/EditorUiState.hpp"
#include "EditorContext.hpp"

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
    void DrawConsoleLogPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawCommandHistoryPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawHotReloadPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawCameraViewPanel(EditorContext& ctx, EditorUiState& ui);
    void DrawSettingsPanel(EditorContext& ctx, EditorUiState& ui);

    // Viewport overlay widgets (stats/tools)
    void DrawViewportOverlay(EditorContext& ctx, EditorUiState& ui);

    // AI panel
    void DrawAIPanel(EditorContext& ctx, EditorUiState& ui);

    // Pump the AI agent's main-thread tool queue. Must be called every frame,
    // independent of the AI panel's visibility, so a hidden/collapsed/inactive
    // panel does not stall in-flight agent tool calls (which marshal back onto the
    // main thread via the dispatcher and otherwise time out).
    void TickAIAgentMainThread(EditorContext& ctx);

    // Floating panels
    void DrawMaterialEditorPanel(EditorContext& ctx, EditorUiState& ui);

    // Cross-panel helpers
    void OpenMaterialEditor(EditorContext& ctx, EditorUiState& ui);
} // namespace Editor
