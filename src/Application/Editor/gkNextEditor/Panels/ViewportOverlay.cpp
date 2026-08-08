#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"
#include "EditorMain.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorActionDispatcher.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
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
                                    engine->RayCast(origin, dir,
                                                       [engine, path](Assets::RayCastResult result) mutable
                                                       {
                                                           const glm::vec3 translation =
                                                               result.Hit ? result.HitPoint : glm::vec3(0.0f);
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
                            ctx.engine.RayCast(origin, dir,
                                                  [scene, materialId](Assets::RayCastResult result)
                                                  {
                                                      if (result.Hit)
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
        constexpr float toolbarItemSpacing = 4.0f;
        const float toolbarHeight = std::ceil(ImGui::GetFontSize() + 22.0f);
        const float toolH = toolbarHeight;
        const float toolW = std::max(60.0f, std::min(kToolIconWidth + 16.0f, size.x - padding * 2.0f));

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

        // Match gkNextRenderer's viewport toolbar metrics and flat-control styling.
        float widestRendererName = 0.0f;
        for (Vulkan::ERendererType rendererType : supportedRenderers)
        {
            widestRendererName =
                std::max(widestRendererName, ImGui::CalcTextSize(Vulkan::GetRendererName(rendererType)).x);
        }
        constexpr float toolbarFramePaddingX = 7.0f;
        constexpr float toolbarFramePaddingY = 3.0f;
        const float comboArrowWidth = ImGui::GetFontSize() + toolbarFramePaddingY * 2.0f;
        const auto comboWidth = [&](float textWidth)
        {
            return std::ceil(textWidth + toolbarFramePaddingX * 2.0f + comboArrowWidth);
        };
        const auto buttonWidth = [&](float textWidth)
        {
            return std::ceil(textWidth + toolbarFramePaddingX * 2.0f);
        };
        const float rendererComboWidth = comboWidth(widestRendererName);
        const float progressiveButtonWidth = buttonWidth(std::max(
            ImGui::CalcTextSize("Progressive").x, ImGui::CalcTextSize("Realtime").x));
        const auto& upscalerInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(std::max(0, userSettings.UpscalerType)));
        const auto& upscaleModeInfo = Rendering::Upscaler::GetUpscaleModeInfo(userSettings.SuperResolution);
        const std::string upscalerLabel = fmt::format("{} · {}", upscalerInfo.name, upscaleModeInfo.name);
        const float upscalerComboWidth = comboWidth(ImGui::CalcTextSize(upscalerLabel.c_str()).x);
        const float showButtonWidth =
            buttonWidth(ImGui::CalcTextSize(ICON_FA_EYE " Show").x);
        const float availableToolbarWidth = std::max(0.0f, size.x - padding * 3.0f - toolW);
        const float fullToolbarWidth = rendererComboWidth + progressiveButtonWidth + upscalerComboWidth +
            showButtonWidth + toolbarItemSpacing * 3.0f + 8.0f;
        const bool showUpscalerSelector = fullToolbarWidth <= availableToolbarWidth;
        const float toolbarWidth = fullToolbarWidth -
            (showUpscalerSelector ? 0.0f : upscalerComboWidth + toolbarItemSpacing);

        NextUI::Theme::FOverlayPanelConfig toolbarConfig{};
        toolbarConfig.WindowId = "ViewportToolbar";
        toolbarConfig.Position = pos + ImVec2(padding, padding);
        toolbarConfig.Size = ImVec2(std::min(toolbarWidth, availableToolbarWidth), toolbarHeight);
        toolbarConfig.Padding = ImVec2(4.0f, 4.0f);
        toolbarConfig.ItemSpacing = ImVec2(toolbarItemSpacing, 0.0f);
        toolbarConfig.Rounding = 5.0f;
        toolbarConfig.BackgroundAlpha = 0.74f;

        if (NextUI::Theme::BeginOverlayPanel(toolbarConfig))
        {
            NextUI::Theme::PushViewportToolbarStyle();

            ImGui::SetNextItemWidth(rendererComboWidth);
            NextUI::Theme::PushViewportPopupStyle();
            if (ImGui::BeginCombo("##ViewportRenderer", Vulkan::GetRendererName(currentRendererType)))
            {
                for (Vulkan::ERendererType rendererType : supportedRenderers)
                {
                    const bool isSelected = rendererType == currentRendererType;
                    if (NextUI::Theme::DrawViewportComboOption(Vulkan::GetRendererName(rendererType), isSelected))
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
            NextUI::Theme::PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Active Renderer");
            ImGui::SameLine();

            const char* renderModeLabel = userSettings.ProgressiveRender ? "Progressive" : "Realtime";
            if (NextUI::Theme::DrawFlatViewportButton(
                    renderModeLabel, "Toggle realtime / progressive rendering",
                    userSettings.ProgressiveRender,
                    ImVec2(progressiveButtonWidth, ImGui::GetFrameHeight())))
            {
                userSettings.ProgressiveRender = !userSettings.ProgressiveRender;
                if (!userSettings.ProgressiveRender)
                {
                    ctx.engine.SetProgressiveRendering(false);
                }
            }
            ImGui::SameLine();

            if (showUpscalerSelector)
            {
                ImGui::SetNextItemWidth(upscalerComboWidth);
                NextUI::Theme::PushViewportPopupStyle();
                if (ImGui::BeginCombo("##ViewportUpscaler", upscalerLabel.c_str()))
                {
                    ImGui::TextDisabled("Upscaler");
                    for (uint32_t rawType = 0;
                         rawType < static_cast<uint32_t>(Rendering::Upscaler::EUpscalerType::Count);
                         ++rawType)
                    {
                        const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(rawType);
                        const bool supported = typeInfo.type == Rendering::Upscaler::EUpscalerType::None ||
                            renderer.SupportsUpscaler(typeInfo.type);
                        const bool selected = rawType == static_cast<uint32_t>(userSettings.UpscalerType);
                        ImGui::BeginDisabled(!supported);
                        if (NextUI::Theme::DrawViewportComboOption(typeInfo.name, selected))
                        {
                            userSettings.UpscalerType = static_cast<int32_t>(rawType);
                            if (!renderer.SupportsFrameGeneration(typeInfo.type))
                            {
                                userSettings.FrameGeneration = false;
                            }
                            renderer.RequestRecreateSwapChain();
                        }
                        ImGui::EndDisabled();
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Quality");
                    for (uint32_t rawMode = 0;
                         rawMode <= static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Auto);
                         ++rawMode)
                    {
                        const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(rawMode);
                        const bool selected = rawMode == userSettings.SuperResolution;
                        if (NextUI::Theme::DrawViewportComboOption(modeInfo.name, selected))
                        {
                            userSettings.SuperResolution = rawMode;
                            renderer.RequestRecreateSwapChain();
                        }
                    }
                    ImGui::EndCombo();
                }
                NextUI::Theme::PopViewportPopupStyle();
                NextUI::Theme::DrawTooltip("Upscaler and quality mode");
                ImGui::SameLine();
            }

            if (NextUI::Theme::DrawFlatViewportButton(
                    ICON_FA_EYE " Show", "Show Flags", false,
                    ImVec2(showButtonWidth, ImGui::GetFrameHeight())))
            {
                ImGui::OpenPopup("ViewportShowFlags");
            }

            NextUI::Theme::PopViewportToolbarStyle();
        }

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

        // AmbientCube brick residency is an engine-internal diagnostic; it stays behind
        // the graphics debug flag so the default viewport is free of internal counters.
        if (renderer.CurrentRendererRequirements().requestAmbientCube &&
            ctx.engine.GetShowFlags().DebugGraphicsPanel)
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
        
        const ImVec2 axisOrigin = pos + ImVec2(38.0f, size.y - 42.0f);
        ImDrawList* foreground = ImGui::GetForegroundDrawList(viewport);
        const Assets::Camera viewportCamera = ctx.editor
            ? ctx.editor->BuildSceneViewportCamera()
            : ctx.scene.GetRenderCamera();
        const glm::mat3 viewRotation(viewportCamera.ModelView);
        struct FPreviewAxis
        {
            const char* label;
            ImU32 color;
            glm::vec3 projected;
        };
        std::array<FPreviewAxis, 3> previewAxes = {{
            {"X", NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger),
             viewRotation * glm::vec3(1.0f, 0.0f, 0.0f)},
            {"Y", NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success),
             viewRotation * glm::vec3(0.0f, 1.0f, 0.0f)},
            {"Z", NextUI::Theme::ColorU32(NextUI::Theme::EColor::Blue),
             viewRotation * glm::vec3(0.0f, 0.0f, 1.0f)},
        }};
        // Draw the axes pointing farther into the view first so nearer axes remain legible.
        std::sort(previewAxes.begin(), previewAxes.end(),
                  [](const FPreviewAxis& lhs, const FPreviewAxis& rhs)
                  {
                      return lhs.projected.z < rhs.projected.z;
                  });

        constexpr float axisLength = 28.0f;
        for (const FPreviewAxis& axis : previewAxes)
        {
            const ImVec2 screenDirection(axis.projected.x, -axis.projected.y);
            const float projectedLength = std::sqrt(
                screenDirection.x * screenDirection.x + screenDirection.y * screenDirection.y);
            if (projectedLength < 0.08f)
            {
                foreground->AddCircleFilled(axisOrigin, 3.0f, axis.color);
                continue;
            }

            const ImVec2 axisEnd = axisOrigin + screenDirection * axisLength;
            const ImVec2 labelDirection = screenDirection * (1.0f / projectedLength);
            const ImVec2 labelSize = ImGui::CalcTextSize(axis.label);
            const ImVec2 labelPos = axisEnd + labelDirection * 6.0f - labelSize * 0.5f;
            foreground->AddLine(axisOrigin, axisEnd, axis.color, 2.0f);
            foreground->AddCircleFilled(axisEnd, 2.5f, axis.color);
            foreground->AddText(labelPos, axis.color, axis.label);
        }
        foreground->AddCircleFilled(
            axisOrigin, 4.0f, NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextMuted));

        NextUI::Theme::FOverlayPanelConfig toolConfig{};
        toolConfig.WindowId = "ViewportTool";
        toolConfig.Position = pos + ImVec2(std::max(padding, size.x - toolW - padding), padding);
        toolConfig.Size = ImVec2(toolW, toolH);
        toolConfig.Padding = ImVec2(4.0f, 4.0f);
        toolConfig.ItemSpacing = ImVec2(toolbarItemSpacing, 0.0f);
        toolConfig.Rounding = 5.0f;
        toolConfig.BackgroundAlpha = 0.74f;

        NextUI::Theme::BeginOverlayPanel(toolConfig);
        NextUI::Theme::PushViewportToolbarStyle();

        const float cameraButtonWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        if (NextUI::Theme::DrawFlatViewportButton(
                ICON_FA_CAMERA, "Camera Options", false,
                ImVec2(cameraButtonWidth, ImGui::GetFrameHeight())))
        {
            ImGui::OpenPopup("ViewportCameraOptions");
        }

        NextUI::Theme::PushViewportPopupStyle();
        ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f));
        if (ImGui::BeginPopup("ViewportCameraOptions"))
        {
            auto& cameras = ctx.scene.GetEnvSettings().cameras;
            const int currentCameraIndex = ctx.engine.GetUserSettings().CameraIdx;
            const char* currentCameraName = "No Camera";
            if (currentCameraIndex >= 0 && currentCameraIndex < static_cast<int>(cameras.size()))
            {
                const std::string& name = cameras[static_cast<size_t>(currentCameraIndex)].name;
                currentCameraName = name.empty() ? "Unnamed Camera" : name.c_str();
            }

            const std::string sceneCameraMenuLabel = fmt::format(
                "{} Scene Cameras    {}    {}", ICON_FA_CAMERA, currentCameraName, ICON_FA_CHEVRON_RIGHT);
            ImGui::BeginDisabled(cameras.empty());
            const bool openSceneCameraMenu = ImGui::Selectable(
                sceneCameraMenuLabel.c_str(), false,
                ImGuiSelectableFlags_DontClosePopups, ImVec2(0.0f, 28.0f));
            const ImVec2 sceneCameraMenuMin = ImGui::GetItemRectMin();
            const ImVec2 sceneCameraMenuMax = ImGui::GetItemRectMax();
            ImGui::EndDisabled();
            if (openSceneCameraMenu)
            {
                ImGui::OpenPopup("ViewportSceneCameras");
            }

            ImGui::SetNextWindowPos(ImVec2(sceneCameraMenuMax.x, sceneCameraMenuMin.y), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f));
            if (ImGui::BeginPopup("ViewportSceneCameras"))
            {
                for (size_t cameraIndex = 0; cameraIndex < cameras.size(); ++cameraIndex)
                {
                    const Assets::Camera& camera = cameras[cameraIndex];
                    const std::string cameraLabel = fmt::format(
                        "{} {}", ICON_FA_CAMERA, camera.name.empty()
                            ? fmt::format("Camera {}", cameraIndex + 1)
                            : camera.name);
                    const bool selected = currentCameraIndex == static_cast<int>(cameraIndex);
                    if (NextUI::Theme::DrawViewportComboOption(cameraLabel.c_str(), selected) && ctx.editor)
                    {
                        ctx.editor->SelectSceneCamera(cameraIndex);
                    }
                }
                ImGui::EndPopup();
            }

            if (NextUI::Theme::DrawViewportComboOption(
                    ICON_FA_ROTATE_LEFT " Restore Scene Default", false) && ctx.editor)
            {
                ctx.editor->ResetToDefaultSceneCamera();
            }
            NextUI::Theme::DrawTooltip("Restore the scene's default camera");

            ImGui::Separator();
            ImGui::TextDisabled("Lens & Exposure");

            float fieldOfView = ctx.editor
                ? ctx.editor->BuildSceneViewportCamera().FieldOfView
                : ctx.scene.GetRenderCamera().FieldOfView;
            ImGui::TextUnformatted("Field of View");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderFloat("##ViewportCameraFov", &fieldOfView, 10.0f, 140.0f, "%.1f deg") && ctx.editor)
            {
                ctx.editor->SetSceneViewportFieldOfView(fieldOfView);
            }

            ImGui::TextUnformatted("Paper White");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderFloat("##ViewportPaperWhite", &userSettings.PaperWhiteNit,
                                   100.0f, 1600.0f, "%.0f nit"))
            {
                ctx.engine.ResetProgressiveRenderingAccumulation();
            }

            ImGui::EndPopup();
        }
        NextUI::Theme::PopViewportPopupStyle();

        NextUI::Theme::PopViewportToolbarStyle();
        NextUI::Theme::EndOverlayPanel();
    }
} // namespace Editor
