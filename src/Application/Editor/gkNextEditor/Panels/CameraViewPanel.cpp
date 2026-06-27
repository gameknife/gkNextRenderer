#include "EditorUi.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <imgui.h>

// Multi-viewport "Camera View" panel: shows a second camera's live render of the current scene,
// produced by the engine's secondary RenderView (offscreen -> bindless sample slot, see
// VulkanBaseRenderer::SetSecondaryViewEnabled / SecondaryViewSampleSlot). The secondary view is
// enabled only while this panel is open, so it costs nothing when hidden.
namespace Editor
{
    void DrawCameraViewPanel(EditorContext& ctx, EditorUiState& uiState)
    {
        Vulkan::VulkanBaseRenderer& renderer = ctx.engine.GetRenderer();

        bool open = uiState.cameraViewPanel;
        // Keep the panel inside the main window (don't let ImGui multi-viewport tear it into a
        // separate OS window), positioned relative to the main viewport's work area so it lands
        // on-screen on first use.
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + 80.0f, mainViewport->WorkPos.y + 80.0f),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, 290.0f), ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin("Camera View", &open);

        // Drive the engine's secondary view from the panel being open (not just expanded) so it
        // bootstraps even on the first frame / when collapsed (1-frame latency is fine).
        renderer.SetSecondaryViewEnabled(open);

        if (visible)
        {
            if (renderer.IsSecondaryViewReady())
            {
                const ImTextureID tex = ctx.ui.RequestImTextureIdRaw(renderer.SecondaryViewSampleSlot());
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x > 1.0f && avail.y > 1.0f)
                {
                    ImGui::Image(tex, avail);
                }
            }
            else
            {
                ImGui::TextUnformatted("Initializing secondary view...");
            }
        }

        ImGui::End();
        uiState.cameraViewPanel = open;
    }
}
