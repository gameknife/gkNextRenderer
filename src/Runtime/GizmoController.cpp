#include "Runtime/GizmoController.hpp"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Command/TransformNodeCommand.hpp"
#include "Runtime/Command/DuplicateNodeCommand.hpp"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"

#include <imgui.h>
#include <memory>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
    constexpr float kToolbarEdgePadding = 5.0f;
}

void GizmoController::EnsureDefaults()
{
    if (operation_ == 0)
    {
        operation_ = static_cast<int>(ImGuizmo::TRANSLATE);
    }
    if (mode_ == 0)
    {
        mode_ = static_cast<int>(ImGuizmo::LOCAL);
    }
}

void GizmoController::HandleShortcuts(const ImGuiIO& io)
{
    if (io.WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        operation_ = static_cast<int>(ImGuizmo::TRANSLATE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        operation_ = static_cast<int>(ImGuizmo::ROTATE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        operation_ = static_cast<int>(ImGuizmo::SCALE);
    }
}

void GizmoController::ResetState()
{
    isUsing_ = false;
    isOver_ = false;
    isShowing_ = false;
    wasUsing_ = false;
    dragActive_ = false;
}

void GizmoController::DrawToolbar()
{
    EnsureDefaults();

    constexpr float kToolbarPadX = 8.0f;
    constexpr float kToolbarPadY = 6.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kToolbarPadX, kToolbarPadY));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
    ImGui::Begin("GizmoToolbar", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDocking);

    if (ImGui::RadioButton("Move", operation_ == static_cast<int>(ImGuizmo::TRANSLATE)))
    {
        operation_ = static_cast<int>(ImGuizmo::TRANSLATE);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", operation_ == static_cast<int>(ImGuizmo::ROTATE)))
    {
        operation_ = static_cast<int>(ImGuizmo::ROTATE);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", operation_ == static_cast<int>(ImGuizmo::SCALE)))
    {
        operation_ = static_cast<int>(ImGuizmo::SCALE);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GizmoController::Draw(NextEngine& engine, const glm::vec2& viewportPos, const glm::vec2& viewportSize)
{
    Assets::Scene& scene = engine.GetScene();
    uint32_t selectedId = scene.GetSelectedId();
    if (selectedId == static_cast<uint32_t>(-1))
    {
        ResetState();
        return;
    }

    Assets::Node* node = scene.GetNodeByInstanceId(selectedId);
    if (node == nullptr)
    {
        ResetState();
        return;
    }

    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        ResetState();
        return;
    }

    isShowing_ = true;

    ImGuiIO& io = ImGui::GetIO();
    HandleShortcuts(io);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 toolbarPos(viewportPos.x + viewportSize.x * 0.5f,
                            viewportPos.y + viewportSize.y - kToolbarEdgePadding);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    DrawToolbar();

    const auto& ubo = engine.GetUniformBufferObject();
    const glm::mat4& view = ubo.ModelView;
    glm::mat4 projection = ubo.Projection;
    projection[1][1] *= -1.0f;

    glm::mat4 worldMatrix = node->WorldTransform();
    Assets::Node* activeNode = node;
    
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
    ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        static_cast<ImGuizmo::OPERATION>(operation_),
        static_cast<ImGuizmo::MODE>(mode_),
        glm::value_ptr(worldMatrix));

    isUsing_ = ImGuizmo::IsUsing();
    isOver_ = ImGuizmo::IsOver();
    if (isUsing_ && !wasUsing_)
    {
        if (io.KeyShift)
        {
            auto duplicateCommand = std::make_unique<DuplicateNodeCommand>(scene, selectedId);
            auto* duplicateCommandPtr = duplicateCommand.get();
            if (engine.ExecuteCommand(std::move(duplicateCommand)))
            {
                const uint32_t newId = duplicateCommandPtr->GetNewInstanceId();
                Assets::Node* newNode = scene.GetNodeByInstanceId(newId);
                if (newNode)
                {
                    selectedId = newId;
                    scene.SetSelectedId(newId);
                    activeNode = newNode;
                }
            }
        }
        dragActive_ = true;
        dragInstanceId_ = selectedId;
        dragStartTranslation_ = activeNode->Translation();
        dragStartRotation_ = activeNode->Rotation();
        dragStartScale_ = activeNode->Scale();
    }
    if (isUsing_)
    {
        glm::mat4 parentWorld(1.0f);
        if (activeNode->GetParent() != nullptr)
        {
            parentWorld = activeNode->GetParent()->WorldTransform();
        }
        glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;

        glm::vec3 scale{};
        glm::quat rotation{};
        glm::vec3 translation{};
        glm::vec3 skew{};
        glm::vec4 perspective{};

        if (glm::decompose(localMatrix, scale, rotation, translation, skew, perspective))
        {
            activeNode->SetTranslation(translation);
            activeNode->SetRotation(rotation);
            activeNode->SetScale(scale);
            activeNode->RecalcTransform(true);
            scene.MarkDirty();
        }
    }
    else if (wasUsing_)
    {
        if (dragActive_)
        {
            TransformSnapshot beforeSnapshot{dragStartTranslation_, dragStartRotation_, dragStartScale_};
            TransformSnapshot afterSnapshot{activeNode->Translation(), activeNode->Rotation(), activeNode->Scale()};
            if (TransformNodeCommand::IsDifferent(beforeSnapshot, afterSnapshot))
            {
                auto command = std::make_unique<TransformNodeCommand>(scene, dragInstanceId_, beforeSnapshot, afterSnapshot);
                engine.ExecuteCommand(std::move(command));
            }
        }
        dragActive_ = false;
    }

    wasUsing_ = isUsing_;

    io.WantCaptureMouse = io.WantCaptureMouse || isOver_ || isUsing_;
}
