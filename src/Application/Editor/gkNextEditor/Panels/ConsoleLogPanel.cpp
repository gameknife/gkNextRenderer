#include "EditorUi.hpp"

#include "Engine/Runtime/Editor/UserInterface.hpp"

#include <imgui.h>
#include "Modules/DevTools/UiDevPanels.hpp"

namespace Editor
{
    void DrawConsoleLogPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("Log", &ui.logPanel))
        {
            ImGui::End();
            return;
        }

        DevTools::FUiDevPanels::Get().DrawConsoleLogOutput("EditorLogOutput", ImVec2(0.0f, 0.0f), true);

        ImGui::End();
    }
} // namespace Editor
