#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorActionDispatcher.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/ImGui.hpp"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <filesystem>
#include <string>

namespace Editor
{
    namespace
    {
        constexpr float kToolIconWidth = 34.0f;

        std::vector<Vulkan::ERendererType> BuildSupportedRendererList(NextEngine& engine)
        {
            std::vector<Vulkan::ERendererType> supportedTypes;
            const bool hasFullAmbientCubeBudget = engine.GetRenderer().HasFullAmbientCubeBudget();
            if (hasFullAmbientCubeBudget)
            {
                supportedTypes = {
                    Vulkan::ERT_SoftwareTracing,
                    Vulkan::ERT_SoftwareModern,
                    Vulkan::ERT_VoxelTracing,
                    Vulkan::ERT_SoftwareModernNoAmbient,
                };
            }
            else
            {
                supportedTypes = {Vulkan::ERT_SoftwareModernNoAmbient};
            }

            if (engine.GetRenderer().SupportsRayTracing() && hasFullAmbientCubeBudget)
            {
                supportedTypes.emplace_back(Vulkan::ERT_PathTracing);
            }

            return supportedTypes;
        }

        bool CanAuthorSceneReferences(const std::string& currentScenePath)
        {
            if (currentScenePath.empty())
            {
                return false;
            }
            std::string ext = std::filesystem::path(currentScenePath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".gltf" || ext == ".glb";
        }
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
                                if (CanAuthorSceneReferences(ui.currentScenePath))
                                {
                                    glm::dvec2 mousePos = ctx.engine.GetMousePos();
                                    glm::vec3 origin;
                                    glm::vec3 dir;
                                    Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(mousePos.x, mousePos.y),
                                                                               origin, dir);

                                    NextEngine* engine = &ctx.engine;
                                    engine->RayCastGPU(origin, dir,
                                                       [engine, path](Assets::RayCastResult result) mutable
                                                       {
                                                           const glm::vec3 translation =
                                                               result.Hitted ? result.HitPoint : glm::vec3(0.0f);
                                                           engine->RequestAddSceneReference(path, translation);
                                                           return true;
                                                       });
                                }
                                else
                                {
                                    SPDLOG_WARN("Scene references can only be authored in glTF/GLB host scenes");
                                }
                            }
                        }
                        else if (data->type == EEditorDragPayloadType::Material)
                        {
                            const uint32_t materialId = data->materialId;
                            glm::dvec2 mousePos = ctx.engine.GetMousePos();
                            glm::vec3 origin;
                            glm::vec3 dir;
                            Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(mousePos.x, mousePos.y), origin, dir);

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

        NextUI::Theme::FOverlayPanelConfig toolbarConfig{};
        toolbarConfig.WindowId = "ViewportToolbar";
        toolbarConfig.Position = pos + ImVec2(padding, padding);
        toolbarConfig.Size = ImVec2(std::min(700.0f, size.x - padding * 2.0f), 36.0f);
        toolbarConfig.Padding = ImVec2(0.0f, 4.0f);
        toolbarConfig.ItemSpacing = ImVec2(6.0f, 0.0f);
        toolbarConfig.BackgroundAlpha = 0.0f;

        NextUI::Theme::BeginOverlayPanel(toolbarConfig);

        auto& overlayState = ui.viewportOverlay;
        Runtime::Config::UserSettings& userSettings = ctx.engine.GetUserSettings();
        auto& renderer = ctx.engine.GetRenderer();
        const auto supportedRenderers = BuildSupportedRendererList(ctx.engine);
        Vulkan::ERendererType currentRendererType = renderer.CurrentLogicRendererType();
        if (std::find(supportedRenderers.begin(), supportedRenderers.end(), currentRendererType) ==
            supportedRenderers.end())
        {
            currentRendererType = supportedRenderers.empty() ? Vulkan::ERT_SoftwareModernNoAmbient
                                                             : supportedRenderers.front();
        }

        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::BeginCombo("##ViewportRenderer", Vulkan::GetRendererName(currentRendererType)))
        {
            for (Vulkan::ERendererType rendererType : supportedRenderers)
            {
                const bool isSelected = rendererType == currentRendererType;
                if (ImGui::Selectable(Vulkan::GetRendererName(rendererType), isSelected))
                {
                    userSettings.RendererType = static_cast<int32_t>(rendererType);
                    if (renderer.CurrentLogicRendererType() != rendererType)
                    {
                        renderer.SwitchLogicRenderer(rendererType);
                    }
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        NextUI::Theme::DrawTooltip("Active Renderer");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(126.0f);
        ImGui::Combo("##ViewportProjection", &overlayState.projectionMode, "Perspective\0Orthographic\0\0");
        NextUI::Theme::DrawTooltip("Camera Projection");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##ViewportDisplayMode", &overlayState.displayMode, "Lit\0Lighting\0Wireframe\0\0");
        NextUI::Theme::DrawTooltip("Display Mode");
        ImGui::SameLine();
        if (NextUI::Theme::ToolbarButton(ICON_FA_EYE " Show", "Show Flags", false, ImVec2(72.0f, 26.0f)))
        {
            ImGui::OpenPopup("ViewportShowFlags");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##AngleSnap", &overlayState.angleSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        NextUI::Theme::DrawTooltip("Angle Snap");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##DistanceSnap", &overlayState.distanceSnap, 0.01f, 0.01f, 10.0f, "%.2f");
        NextUI::Theme::DrawTooltip("Distance Snap");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Combo("##ViewportCamera", &overlayState.cameraIndex, "Camera 0\0Editor Cam\0\0");
        NextUI::Theme::DrawTooltip("Active Camera");

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

        NextUI::Theme::EndOverlayPanel();

        const float toolH = kToolIconWidth;

        const uint32_t progressiveAccumulatedFrames = ctx.engine.GetProgressiveRenderAccumulatedFrames();
        const uint32_t progressiveTargetFrames = ctx.engine.GetProgressiveRenderTargetFrames();
        const int progressiveDigits = static_cast<int>(std::max(
            std::to_string(progressiveAccumulatedFrames).size(),
            std::to_string(progressiveTargetFrames).size()));
        const std::string progressiveFramesText =
            fmt::format("{:>{}}/{:>{}}",
                        progressiveAccumulatedFrames, progressiveDigits,
                        progressiveTargetFrames, progressiveDigits);
        const std::string progressiveTemplateText =
            fmt::format("{0:0>{1}}/{0:0>{1}}", 0, progressiveDigits);
        const float progressiveValueWidth = ImGui::CalcTextSize(progressiveTemplateText.c_str()).x;
        const float progressiveLabelWidth = ImGui::CalcTextSize("Render:").x;
        const float progressivePanelWidth = progressiveLabelWidth + progressiveValueWidth + 30.0f;

        NextUI::Theme::FOverlayPanelConfig progressiveConfig{};
        progressiveConfig.WindowId = "ViewportProgressiveStatus";
        progressiveConfig.Position = pos + ImVec2(
            std::max(padding, size.x - progressivePanelWidth - padding),
            padding + toolH + 8.0f);
        progressiveConfig.Size = ImVec2(progressivePanelWidth, 28.0f);
        progressiveConfig.Padding = ImVec2(10.0f, 4.0f);
        progressiveConfig.ItemSpacing = ImVec2(8.0f, 0.0f);
        progressiveConfig.BackgroundAlpha = 0.0f;

        NextUI::Theme::BeginOverlayPanel(progressiveConfig);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Render:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(
            ctx.engine.IsProgressiveRendering() ? NextUI::Theme::EColor::Text : NextUI::Theme::EColor::TextMuted));
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - progressiveValueWidth - ImGui::GetStyle().WindowPadding.x);
        ImGui::TextUnformatted(progressiveFramesText.c_str());
        ImGui::PopStyleColor();
        NextUI::Theme::EndOverlayPanel();

        if (renderer.CurrentRendererRequirements().requestAmbientCube)
        {
            uint32_t activeAmbientBricks = 0u;
            const uint32_t ambientCascadeCapacity = ctx.scene.AmbientCubeCascadeCapacity();
            for (uint32_t cascadeIndex = 0; cascadeIndex < ambientCascadeCapacity; ++cascadeIndex)
            {
                activeAmbientBricks += ctx.scene.AmbientActiveBrickCount(cascadeIndex);
            }
            const uint32_t totalAmbientBricks =
                ambientCascadeCapacity * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
            const std::string ambientBrickText =
                fmt::format("{}/{}", activeAmbientBricks, totalAmbientBricks);
            const float ambientBrickValueWidth = ImGui::CalcTextSize(ambientBrickText.c_str()).x;
            const float ambientBrickLabelWidth = ImGui::CalcTextSize("AC Bricks:").x;
            const float ambientBrickPanelWidth =
                ambientBrickLabelWidth + ambientBrickValueWidth + 30.0f;

            NextUI::Theme::FOverlayPanelConfig ambientBrickConfig{};
            ambientBrickConfig.WindowId = "ViewportAmbientBrickStatus";
            ambientBrickConfig.Position = pos + ImVec2(
                std::max(padding, size.x - ambientBrickPanelWidth - padding),
                padding + toolH + 44.0f);
            ambientBrickConfig.Size = ImVec2(ambientBrickPanelWidth, 28.0f);
            ambientBrickConfig.Padding = ImVec2(10.0f, 4.0f);
            ambientBrickConfig.ItemSpacing = ImVec2(8.0f, 0.0f);
            ambientBrickConfig.BackgroundAlpha = 0.0f;

            NextUI::Theme::BeginOverlayPanel(ambientBrickConfig);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("AC Bricks:");
            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetWindowContentRegionMax().x - ambientBrickValueWidth - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextUnformatted(ambientBrickText.c_str());
            NextUI::Theme::DrawTooltip("AmbientCube resident bricks / total bricks across allocated cascades");
            NextUI::Theme::EndOverlayPanel();
        }
        
        const ImVec2 axisOrigin = pos + ImVec2(26.0f, size.y - 42.0f);
        ImDrawList* foreground = ImGui::GetForegroundDrawList(viewport);
        foreground->AddCircleFilled(axisOrigin, 4.0f, NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextMuted));
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(28.0f, 0.0f),
                            NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger), 2.0f);
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(0.0f, -28.0f),
                            NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success), 2.0f);
        foreground->AddLine(axisOrigin, axisOrigin + ImVec2(-18.0f, 18.0f),
                            NextUI::Theme::ColorU32(NextUI::Theme::EColor::Blue), 2.0f);
        foreground->AddText(axisOrigin + ImVec2(32.0f, -7.0f), NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger),
                            "X");
        foreground->AddText(axisOrigin + ImVec2(-4.0f, -44.0f), NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success),
                            "Y");
        foreground->AddText(axisOrigin + ImVec2(-34.0f, 18.0f), NextUI::Theme::ColorU32(NextUI::Theme::EColor::Blue),
                            "Z");

        float toolW = kToolIconWidth + 16.0f;
        toolW = std::max(60.0f, std::min(toolW, size.x - padding * 2.0f));

        NextUI::Theme::FOverlayPanelConfig toolConfig{};
        toolConfig.WindowId = "ViewportTool";
        toolConfig.Position = pos + ImVec2(std::max(padding, size.x - toolW - padding), padding);
        toolConfig.Size = ImVec2(toolW, toolH);
        toolConfig.Padding = ImVec2(3.0f, 3.0f);
        toolConfig.ItemSpacing = ImVec2(0.0f, 0.0f);
        toolConfig.BackgroundAlpha = 0.0f;

        NextUI::Theme::BeginOverlayPanel(toolConfig);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        const float startX = ImGui::GetCursorPosX();
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(startX + std::max(0.0f, availW - kToolIconWidth));
        NextUI::Theme::IconButton(ICON_FA_CAMERA, "Camera Options (placeholder)", false,
                                     ImVec2(kToolIconWidth, kToolIconWidth));

        ImGui::PopStyleVar();
        NextUI::Theme::EndOverlayPanel();
    }
} // namespace Editor
