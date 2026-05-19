#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorActionDispatcher.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Editor/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/GizmoController.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <string>

namespace Editor
{
    namespace
    {
        constexpr float kToolIconWidth = 34.0f;
    }

    void DrawViewportOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ui.viewportOnMainViewport)
        {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 pos = ui.viewportContentPos;
        const ImVec2 size = ui.viewportContentSize;
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }

        if (ui.viewportOnMainViewport && ImGui::GetDragDropPayload() != nullptr)
        {
            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowBgAlpha(0.0f);

            ImGuiWindowFlags dropFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("ViewportDropTarget", nullptr, dropFlags);
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();

            ImGui::InvisibleButton("ViewportDropTargetBtn", size);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEditorDragDropPayload))
                {
                    if (payload->DataSize == sizeof(EditorDragDropPayload))
                    {
                        const auto* data = static_cast<const EditorDragDropPayload*>(payload->Data);
                        if (data->type == EEditorDragPayloadType::Scene)
                        {
                            std::string path = data->path;
                            const bool hasScene = !ctx.scene.Nodes().empty();
                            if (!hasScene)
                            {
                                ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, path);
                            }
                            else
                            {
                                glm::dvec2 mousePos = ctx.engine.GetMousePos();
                                glm::vec3 origin;
                                glm::vec3 dir;
                                NextEngineHelper::GetScreenToWorldRay(glm::vec2(mousePos.x, mousePos.y), origin, dir);

                                NextEngine* engine = &ctx.engine;
                                engine->RayCastGPU(origin, dir,
                                                   [engine, path](Assets::RayCastResult result) mutable
                                                   {
                                                        NextEngine::FSceneLoadRequest request{.filename = path, .append = true};
                                                        if (result.Hitted)
                                                        {
                                                            request.placeOnHit = true;
                                                            request.hitPosition = result.HitPoint;
                                                        }
                                                        engine->RequestLoadScene(std::move(request));
                                                        return true;
                                                    });
                            }
                        }
                        else if (data->type == EEditorDragPayloadType::Material)
                        {
                            const uint32_t materialId = data->materialId;
                            glm::dvec2 mousePos = ctx.engine.GetMousePos();
                            glm::vec3 origin;
                            glm::vec3 dir;
                            NextEngineHelper::GetScreenToWorldRay(glm::vec2(mousePos.x, mousePos.y), origin, dir);

                            Assets::Scene* scene = &ctx.scene;
                            ctx.engine.RayCastGPU(origin, dir,
                                                  [scene, materialId](Assets::RayCastResult result)
                                                  {
                                                      if (result.Hitted)
                                                      {
                                                          Assets::Node* node =
                                                              scene->GetNodeByInstanceId(result.InstanceId);
                                                          if (node)
                                                          {
                                                              auto render =
                                                                  node->GetComponent<Runtime::RenderComponent>();
                                                              if (render && render->IsDrawable())
                                                              {
                                                                  auto mats = render->GetMaterials();
                                                                  for (auto& mat : mats)
                                                                  {
                                                                      mat = materialId;
                                                                  }
                                                                  render->SetMaterials(mats);
                                                                  scene->MarkDirty();
                                                              }
                                                          }
                                                      }
                                                      return true;
                                                  });
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::End();
        }

        constexpr float padding = 8.0f;
        constexpr float statPadX = 10.0f;
        constexpr float statPadY = 8.0f;

        ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        Runtime::UiTheme::FOverlayPanelConfig toolbarConfig{};
        toolbarConfig.WindowId = "ViewportToolbar";
        toolbarConfig.Position = pos + ImVec2(padding, padding);
        toolbarConfig.Size = ImVec2(std::min(700.0f, size.x - padding * 2.0f), 36.0f);
        toolbarConfig.Padding = ImVec2(8.0f, 4.0f);
        toolbarConfig.ItemSpacing = ImVec2(6.0f, 0.0f);
        toolbarConfig.BackgroundAlpha = 0.82f;

        Runtime::UiTheme::BeginOverlayPanel(toolbarConfig);

        static int projectionMode = 0;
        static int displayMode = 0;
        static int cameraIndex = 0;
        static float angleSnap = 10.0f;
        static float distanceSnap = 0.25f;

        ImGui::SetNextItemWidth(126.0f);
        ImGui::Combo("##ViewportProjection", &projectionMode, "Perspective\0Orthographic\0\0");
        Runtime::UiTheme::DrawTooltip("Camera Projection");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##ViewportDisplayMode", &displayMode, "Lit\0Lighting\0Wireframe\0\0");
        Runtime::UiTheme::DrawTooltip("Display Mode");
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_EYE " Show", "Show Flags", false, ImVec2(72.0f, 26.0f)))
        {
            ImGui::OpenPopup("ViewportShowFlags");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##AngleSnap", &angleSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        Runtime::UiTheme::DrawTooltip("Angle Snap");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##DistanceSnap", &distanceSnap, 0.01f, 0.01f, 10.0f, "%.2f");
        Runtime::UiTheme::DrawTooltip("Distance Snap");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Combo("##ViewportCamera", &cameraIndex, "Camera 0\0Editor Cam\0\0");
        Runtime::UiTheme::DrawTooltip("Active Camera");

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        if (ImGui::BeginPopup("ViewportShowFlags"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
            auto& showFlags = ctx.engine.GetShowFlags();
            Utilities::UI::DrawShowFlagsCommon(showFlags);

            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        Runtime::UiTheme::EndOverlayPanel();

        const double smoothDelta = ctx.engine.GetSmoothDeltaSeconds();
        const double frameRate = smoothDelta > 0.0 ? (1.0 / smoothDelta) : 0.0;
        const float gpuMs = ctx.engine.GpuTimer() ? ctx.engine.GpuTimer()->GetGpuTime("[gpu time]") : 0.0f;
        const auto& gpuDrivenStat = ctx.scene.GetGpuDrivenStat();
        const uint32_t drawCalls = gpuDrivenStat.ProcessedCount > gpuDrivenStat.CulledCount
            ? gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount
            : 0;
        const uint32_t triangles = gpuDrivenStat.TriangleCount > gpuDrivenStat.CulledTriangleCount
            ? gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount
            : 0;

        const float statW = 192.0f;
        const float statH = ImGui::GetTextLineHeightWithSpacing() * 6.0f + statPadY * 2.0f;
        Runtime::UiTheme::FOverlayPanelConfig statConfig{};
        statConfig.WindowId = "ViewportStat";
        statConfig.Position = pos + ImVec2(padding, padding + 40.0f);
        statConfig.Size = ImVec2(statW, statH);
        statConfig.Padding = ImVec2(statPadX, statPadY);
        statConfig.ItemSpacing = ImVec2(4.0f, 1.0f);
        statConfig.BackgroundAlpha = 0.78f;

        Runtime::UiTheme::BeginOverlayPanel(statConfig);
        ImGui::PushStyleColor(ImGuiCol_Text, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Text));
        ImGui::Text("FPS %.0f (%.2f ms)", frameRate, smoothDelta * 1000.0);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted));
        ImGui::Text("GPU %.2f ms", gpuMs);
        ImGui::Text("Frame %u", ctx.engine.GetTotalFrames());
        ImGui::Text("Draw Calls %s", Utilities::metricFormatter(static_cast<double>(drawCalls), "").c_str());
        ImGui::Text("Triangles %s", Utilities::metricFormatter(static_cast<double>(triangles), "").c_str());
        ImGui::Text("Res %.0fx%.0f", size.x, size.y);
        ImGui::PopStyleColor();
        Runtime::UiTheme::EndOverlayPanel();

        // Gizmo status overlay (operation + space)
        if (ctx.gizmoController && ctx.gizmoController->IsShowing())
        {
            auto GetOperationName = [](int op) -> const char*
            {
                switch (op)
                {
                case ImGuizmo::TRANSLATE: return "Translate";
                case ImGuizmo::ROTATE:    return "Rotate";
                case ImGuizmo::SCALE:     return "Scale";
                default:                  return "?";
                }
            };
            constexpr const char* kSpaceNames[] = { "Local", "World" };

            int op = ctx.gizmoController->Operation();
            int mode = ctx.gizmoController->Mode();
            std::string gizmoText = std::string(GetOperationName(op)) + " \302\267 " +
                                    kSpaceNames[mode == ImGuizmo::LOCAL ? 0 : 1];

            ImVec2 gizmoSize(ImGui::CalcTextSize(gizmoText.c_str()).x + statPadX * 2.0f, statH);
            const float gizmoY = pos.y + padding + 44.0f + statH + 4.0f;

            Runtime::UiTheme::FOverlayPanelConfig gizmoConfig{};
            gizmoConfig.WindowId = "GizmoStatus";
            gizmoConfig.Position = ImVec2(pos.x + padding, gizmoY);
            gizmoConfig.Size = gizmoSize;
            gizmoConfig.Padding = ImVec2(statPadX, statPadY);
            gizmoConfig.BackgroundAlpha = 0.74f;

            Runtime::UiTheme::BeginOverlayPanel(gizmoConfig);
            ImGui::PushStyleColor(ImGuiCol_Text, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted));
            ImGui::TextUnformatted(gizmoText.c_str());
            ImGui::PopStyleColor();
            Runtime::UiTheme::EndOverlayPanel();
        }

        const ImVec2 axisOrigin = pos + ImVec2(26.0f, size.y - 42.0f);
        ImDrawList* foreground = ImGui::GetForegroundDrawList(viewport);
        foreground->AddCircleFilled(axisOrigin, 4.0f, Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::TextMuted));
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(28.0f, 0.0f),
                            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Danger), 2.0f);
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(0.0f, -28.0f),
                            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Success), 2.0f);
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(-18.0f, 18.0f),
                            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Blue), 2.0f);
        foreground->AddText(axisOrigin + ImVec2(32.0f, -7.0f), Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Danger),
                            "X");
        foreground->AddText(axisOrigin + ImVec2(-4.0f, -44.0f), Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Success),
                            "Y");
        foreground->AddText(axisOrigin + ImVec2(-34.0f, 18.0f), Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Blue),
                            "Z");

        const float toolH = kToolIconWidth;
        float toolW = kToolIconWidth + 16.0f;
        toolW = std::max(60.0f, std::min(toolW, size.x - padding * 2.0f));

        Runtime::UiTheme::FOverlayPanelConfig toolConfig{};
        toolConfig.WindowId = "ViewportTool";
        toolConfig.Position = pos + ImVec2(std::max(padding, size.x - toolW - padding), padding);
        toolConfig.Size = ImVec2(toolW, toolH);
        toolConfig.Padding = ImVec2(3.0f, 3.0f);
        toolConfig.ItemSpacing = ImVec2(0.0f, 0.0f);
        toolConfig.BackgroundAlpha = 0.78f;

        Runtime::UiTheme::BeginOverlayPanel(toolConfig);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        const float startX = ImGui::GetCursorPosX();
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(startX + std::max(0.0f, availW - kToolIconWidth));
        Runtime::UiTheme::IconButton(ICON_FA_CAMERA, "Camera Options (placeholder)", false,
                                     ImVec2(kToolIconWidth, kToolIconWidth));

        ImGui::PopStyleVar();
        Runtime::UiTheme::EndOverlayPanel();
    }
} // namespace Editor
