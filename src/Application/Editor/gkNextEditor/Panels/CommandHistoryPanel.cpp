#include "EditorUi.hpp"

#include <imgui.h>

#include "Engine/Runtime/Engine.hpp"

namespace Editor
{
    void DrawCommandHistoryPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("Command History", &ui.commandHistoryPanel))
        {
            ImGui::End();
            return;
        }

        CommandHistory& history = ctx.engine.GetCommandHistory();

        const bool canUndo = history.CanUndo();
        const bool canRedo = history.CanRedo();

        ImGui::Text("Undo: %zu", history.GetUndoCount());
        ImGui::SameLine();
        ImGui::Text("Redo: %zu", history.GetRedoCount());

        bool mergeEnabled = history.IsMergeEnabled();
        if (ImGui::Checkbox("Merge Continuous Edits", &mergeEnabled))
        {
            history.SetMergeEnabled(mergeEnabled);
        }

        ImGui::SameLine();
        const bool inGroup = history.IsInGroup();
        ImGui::TextDisabled("Grouping: %s", inGroup ? "On" : "Off");

        if (ImGui::Button("Undo", ImVec2(80.0f, 0.0f)))
        {
            if (canUndo)
            {
                history.Undo();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo", ImVec2(80.0f, 0.0f)))
        {
            if (canRedo)
            {
                history.Redo();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(80.0f, 0.0f)))
        {
            history.Clear();
        }

        ImGui::Separator();

        ImGui::BeginChild("CommandHistoryList", ImVec2(0.0f, 0.0f), true);
        ImGui::TextDisabled("Undo Stack (top = most recent)");
        const auto undoDescriptions = history.GetUndoStackDescriptions();
        for (size_t i = 0; i < undoDescriptions.size(); ++i)
        {
            ImGui::Text("%zu: %s", i + 1, undoDescriptions[i].c_str());
        }

        ImGui::Separator();
        ImGui::TextDisabled("Redo Stack (top = most recent)");
        const auto redoDescriptions = history.GetRedoStackDescriptions();
        for (size_t i = 0; i < redoDescriptions.size(); ++i)
        {
            ImGui::Text("%zu: %s", i + 1, redoDescriptions[i].c_str());
        }
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace Editor
