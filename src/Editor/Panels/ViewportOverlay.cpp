#include "Editor/EditorUi.hpp"

#include "Editor/EditorDragDrop.hpp"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/ImGui.hpp"
#include "Utilities/Math.hpp"

#include <string>

namespace Editor
{
    namespace
    {
        constexpr float kToolIconWidth = 32.0f;
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
                                                       NextEngine::SceneAppendOptions options{};
                                                       if (result.Hitted)
                                                       {
                                                           options.placeOnHit = true;
                                                           options.hitPosition = result.HitPoint;
                                                       }
                                                       engine->RequestLoadSceneAdd(path, options);
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
                                                                  auto& mats = render->Materials();
                                                                  for (auto& mat : mats)
                                                                  {
                                                                      mat = materialId;
                                                                  }
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

        constexpr float padding = 5.0f;
        constexpr float statPadX = 8.0f;
        constexpr float statPadY = 6.0f;
        const float statW = 240.0f;
        const float statH = ImGui::GetFrameHeight() + statPadY * 2.0f;

        ImGui::SetNextWindowPos(pos + ImVec2(padding, padding));
        ImGui::SetNextWindowSize(ImVec2(statW, statH));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(statPadX, statPadY));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));

        ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("ViewportStat", nullptr, windowFlags);

        const double smoothDelta = ctx.engine.GetSmoothDeltaSeconds();
        const double frameRate = smoothDelta > 0.0 ? (1.0 / smoothDelta) : 0.0;
        const ImGuiIO& io = ImGui::GetIO();

        auto DrawBoolDot = [](const char* label, bool value)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float radius = 4.0f;
            const float spacing = 6.0f;
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float centerY = cursor.y + ImGui::GetFrameHeight() * 0.5f;
            const ImU32 color = value ? IM_COL32(80, 220, 120, 255) : IM_COL32(220, 80, 80, 255);
            drawList->AddCircleFilled(ImVec2(cursor.x + radius, centerY), radius, color);
            ImGui::Dummy(ImVec2(radius * 2.0f + spacing, ImGui::GetFrameHeight()));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(label);
        };

        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS %.0f", frameRate);
        ImGui::SameLine();
        DrawBoolDot("Mouse", io.WantCaptureMouse);
        ImGui::SameLine();
        DrawBoolDot("Key", io.WantCaptureKeyboard);
        ImGui::SameLine();
        DrawBoolDot("Text", io.WantTextInput);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        const float toolH = kToolIconWidth;
        float toolW = kToolIconWidth + 16.0f;
        toolW = std::max(60.0f, std::min(toolW, size.x - padding * 2.0f));

        ImGui::SetNextWindowPos(pos + ImVec2(std::max(padding, size.x - toolW - padding), padding));
        ImGui::SetNextWindowSize(ImVec2(toolW, toolH));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("ViewportTool", nullptr, windowFlags);


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        const float startX = ImGui::GetCursorPosX();
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(startX + std::max(0.0f, availW - kToolIconWidth));
        if (ImGui::Button(ICON_FA_EYE, ImVec2(kToolIconWidth, kToolIconWidth)))
        {
            ImGui::OpenPopup("ViewportShowFlags");
        }
        BUTTON_TOOLTIP("Show Flags")

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

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();

        ImGui::End();
    }
} // namespace Editor
