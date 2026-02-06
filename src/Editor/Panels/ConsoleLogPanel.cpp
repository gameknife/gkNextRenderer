#include "Editor/EditorUi.hpp"

#include "Runtime/Editor/UserInterface.hpp"

#include <imgui.h>

namespace Editor
{
    void DrawConsoleLogPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("Log", &ui.logPanel))
        {
            ImGui::End();
            return;
        }

        ctx.ui.DrawConsoleLogOutput("EditorLogOutput", ImVec2(0.0f, 0.0f), true);

        ImGui::End();
    }
} // namespace Editor
