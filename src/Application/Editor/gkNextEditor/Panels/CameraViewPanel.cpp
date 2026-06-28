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

        ImGui::SetNextWindowSize(ImVec2(480.0f, 290.0f), ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin("Camera View", &open);
        
        renderer.SetSecondaryViewEnabled(open);

        if (visible)
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 1.0f && avail.y > 1.0f)
            {
                renderer.SetSecondaryViewRenderExtent({
                    static_cast<uint32_t>(avail.x),
                    static_cast<uint32_t>(avail.y)
                });
            }

            if (renderer.IsSecondaryViewReady())
            {
                const ImTextureID tex = ctx.ui.RequestImTextureIdRaw(renderer.SecondaryViewSampleSlot());
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
