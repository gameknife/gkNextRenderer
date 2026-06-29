#include "EditorUi.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Rendering/Preview/RenderPreviewServices.hpp"
#include "EditorMain.h"

#include <imgui.h>
#include <imgui_internal.h>

// Multi-viewport "Camera View" panel: shows a second camera's live render of the current scene,
// produced by the engine's offscreen RenderView. The view is enabled only while this panel is open,
// so it costs nothing when hidden.
namespace Editor
{
    namespace
    {
        EEditorViewportId CameraViewId(size_t viewIndex)
        {
            switch (viewIndex)
            {
            case 0:
                return EEditorViewportId::CameraView0;
            case 1:
                return EEditorViewportId::CameraView1;
            default:
                return EEditorViewportId::CameraView2;
            }
        }

        void DrawCameraViewPanel(EditorContext& ctx, EditorUiState& uiState, size_t viewIndex)
        {
            Vulkan::OffscreenRenderViewController& offscreenViews =
                ctx.engine.GetRenderer().Preview().OffscreenViews();
            EditorCameraViewState& cameraView = uiState.cameraViews[viewIndex];
            cameraView.hovered = false;
            cameraView.focused = false;

            bool open = cameraView.open;
            const std::string title = viewIndex == 0
                ? "Camera View"
                : fmt::format("Camera View {}", viewIndex + 1);

            ImGui::SetNextWindowSize(ImVec2(480.0f, 290.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(
                ImVec2(1020.0f + static_cast<float>(viewIndex) * 32.0f,
                       360.0f + static_cast<float>(viewIndex) * 32.0f),
                ImGuiCond_FirstUseEver);
            const bool visible = ImGui::Begin(title.c_str(), &open);
            ImGuiWindow* cameraViewWindow = ImGui::GetCurrentWindow();

            offscreenViews.SetEnabled(static_cast<uint32_t>(viewIndex), open);
            if (!open)
            {
                offscreenViews.ClearCameraOverride(static_cast<uint32_t>(viewIndex));
                cameraView.contentPos = ImVec2(0.0f, 0.0f);
                cameraView.contentSize = ImVec2(0.0f, 0.0f);
                if (uiState.activeViewport == CameraViewId(viewIndex))
                {
                    uiState.activeViewport = EEditorViewportId::Scene;
                }
            }

            if (visible)
            {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x > 1.0f && avail.y > 1.0f)
                {
                    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
                    cameraView.contentPos = imagePos;
                    cameraView.contentSize = avail;

                    offscreenViews.SetRenderExtent(
                        static_cast<uint32_t>(viewIndex),
                        {static_cast<uint32_t>(avail.x), static_cast<uint32_t>(avail.y)});
                    if (ctx.editor != nullptr)
                    {
                        ctx.editor->SyncCameraViewRendererCamera(viewIndex, glm::vec2(avail.x, avail.y));
                    }
                }

                if (offscreenViews.IsReady(static_cast<uint32_t>(viewIndex)))
                {
                    const ImTextureID tex =
                        ctx.ui.RequestImTextureIdRaw(offscreenViews.SampleSlot(static_cast<uint32_t>(viewIndex)));
                    if (avail.x > 1.0f && avail.y > 1.0f)
                    {
                        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
                        const ImVec2 imageMax = imageMin + avail;
                        ImGui::GetWindowDrawList()->AddImage(tex, imageMin, imageMax);
                        ImGui::Dummy(avail);
                        const ImVec2 mousePos = ImGui::GetIO().MousePos;
                        cameraView.hovered =
                            mousePos.x >= imageMin.x && mousePos.y >= imageMin.y &&
                            mousePos.x < imageMax.x && mousePos.y < imageMax.y;
                        cameraView.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        const bool cameraViewClicked = cameraView.hovered &&
                            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                             ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                             ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
                        if (cameraView.focused || cameraViewClicked)
                        {
                            uiState.activeViewport = CameraViewId(viewIndex);
                        }

                        if (ctx.editor != nullptr && uiState.activeViewport == CameraViewId(viewIndex))
                        {
                            ctx.editor->DrawGizmo(
                                glm::vec2(cameraView.contentPos.x, cameraView.contentPos.y),
                                glm::vec2(cameraView.contentSize.x, cameraView.contentSize.y),
                                offscreenViews.LastUniformBufferObject(static_cast<uint32_t>(viewIndex)),
                                cameraViewWindow);
                        }
                    }
                }
                else
                {
                    ImGui::TextUnformatted("Initializing secondary view...");
                }
            }
            else
            {
                cameraView.contentPos = ImVec2(0.0f, 0.0f);
                cameraView.contentSize = ImVec2(0.0f, 0.0f);
                if (uiState.activeViewport == CameraViewId(viewIndex))
                {
                    uiState.activeViewport = EEditorViewportId::Scene;
                }
            }

            ImGui::End();
            cameraView.open = open;
            if (viewIndex == 0)
            {
                uiState.cameraViewPanel = open;
            }
        }
    } // namespace

    void DrawCameraViewPanel(EditorContext& ctx, EditorUiState& uiState)
    {
        uiState.cameraViewPanel = uiState.cameraViews[0].open;
        for (size_t viewIndex = 0; viewIndex < kMaxCameraViewports; ++viewIndex)
        {
            if (uiState.cameraViews[viewIndex].open)
            {
                DrawCameraViewPanel(ctx, uiState, viewIndex);
            }
            else
            {
                auto& cameraView = uiState.cameraViews[viewIndex];
                cameraView.hovered = false;
                cameraView.focused = false;
                cameraView.contentPos = ImVec2(0.0f, 0.0f);
                cameraView.contentSize = ImVec2(0.0f, 0.0f);
                auto& offscreenViews = ctx.engine.GetRenderer().Preview().OffscreenViews();
                offscreenViews.SetEnabled(static_cast<uint32_t>(viewIndex), false);
                offscreenViews.ClearCameraOverride(static_cast<uint32_t>(viewIndex));
                if (uiState.activeViewport == CameraViewId(viewIndex))
                {
                    uiState.activeViewport = EEditorViewportId::Scene;
                }
                if (viewIndex == 0)
                {
                    uiState.cameraViewPanel = false;
                }
            }
        }

        if (!uiState.cameraViews[0].open)
        {
            uiState.cameraViewPanel = false;
        }
    }
}
