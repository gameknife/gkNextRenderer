#define GLM_ENABLE_EXPERIMENTAL
#include <imgui.h>
#include "Runtime/Editor/GizmoController.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Command/DuplicateNodesCommand.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Command/TransformNodesCommand.hpp"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"

#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <memory>
#include <unordered_set>

namespace
{
    constexpr float kToolbarEdgePadding = 5.0f;
    constexpr uint32_t InvalidNodeId = static_cast<uint32_t>(-1);

    std::vector<uint32_t> BuildSelectionList(Assets::Scene& scene)
    {
        std::vector<uint32_t> selectedIds = scene.GetSelectedIds();
        if (selectedIds.empty())
        {
            const uint32_t selectedId = scene.GetSelectedId();
            if (selectedId != InvalidNodeId)
            {
                selectedIds.push_back(selectedId);
            }
        }

        selectedIds.erase(
            std::remove_if(
                selectedIds.begin(),
                selectedIds.end(),
                [&scene](uint32_t id)
                {
                    return id == InvalidNodeId || scene.GetNodeByInstanceId(id) == nullptr;
                }),
            selectedIds.end());

        return selectedIds;
    }

    std::vector<uint32_t> BuildRootSelection(Assets::Scene& scene, const std::vector<uint32_t>& sourceIds)
    {
        std::vector<uint32_t> orderedUnique;
        orderedUnique.reserve(sourceIds.size());

        std::unordered_set<uint32_t> dedupSet;
        dedupSet.reserve(sourceIds.size());
        for (uint32_t id : sourceIds)
        {
            if (id == InvalidNodeId || dedupSet.contains(id))
            {
                continue;
            }

            if (scene.GetNodeByInstanceId(id) == nullptr)
            {
                continue;
            }

            dedupSet.insert(id);
            orderedUnique.push_back(id);
        }

        std::vector<uint32_t> roots;
        roots.reserve(orderedUnique.size());
        for (uint32_t id : orderedUnique)
        {
            Assets::Node* node = scene.GetNodeByInstanceId(id);
            if (node == nullptr)
            {
                continue;
            }

            bool hasSelectedAncestor = false;
            for (Assets::Node* parent = node->GetParent(); parent != nullptr; parent = parent->GetParent())
            {
                if (dedupSet.contains(parent->GetInstanceId()))
                {
                    hasSelectedAncestor = true;
                    break;
                }
            }

            if (!hasSelectedAncestor)
            {
                roots.push_back(id);
            }
        }

        return roots;
    }

    glm::mat4 BuildGizmoMatrix(Assets::Scene& scene, Assets::Node* activeNode, bool useSelectionBounds)
    {
        if (!useSelectionBounds || activeNode == nullptr)
        {
            return activeNode ? activeNode->WorldTransform() : glm::mat4(1.0f);
        }

        glm::vec3 center;
        float radius = 0.0f;
        if (scene.GetSelectedNodeBounds(center, radius))
        {
            return glm::translate(glm::mat4(1.0f), center);
        }

        return activeNode->WorldTransform();
    }
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

    // Don't handle gizmo shortcuts when right mouse is pressed (camera movement mode)
    if (io.MouseDown[1])
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
    if (ImGui::IsKeyPressed(ImGuiKey_Q))
    {
        mode_ = (mode_ == static_cast<int>(ImGuizmo::LOCAL))
                    ? static_cast<int>(ImGuizmo::WORLD)
                    : static_cast<int>(ImGuizmo::LOCAL);
    }
}

void GizmoController::ResetState()
{
    isUsing_ = false;
    isOver_ = false;
    isShowing_ = false;
    wasUsing_ = false;
    dragActive_ = false;
    dragInstanceIds_.clear();
    dragStartWorldMatrices_.clear();
    dragStartSnapshots_.clear();
    dragStartGizmoMatrix_ = glm::mat4(1.0f);
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
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Translate (W)"); }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", operation_ == static_cast<int>(ImGuizmo::ROTATE)))
    {
        operation_ = static_cast<int>(ImGuizmo::ROTATE);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rotate (E)"); }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", operation_ == static_cast<int>(ImGuizmo::SCALE)))
    {
        operation_ = static_cast<int>(ImGuizmo::SCALE);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Scale (R)"); }
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    if (ImGui::RadioButton("Pivot", pivotMode_ == static_cast<int>(EGizmoPivotMode::Pivot)))
    {
        pivotMode_ = static_cast<int>(EGizmoPivotMode::Pivot);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Use individual pivot for each selected node"); }
    ImGui::SameLine();
    if (ImGui::RadioButton("Bounds", pivotMode_ == static_cast<int>(EGizmoPivotMode::SelectionBounds)))
    {
        pivotMode_ = static_cast<int>(EGizmoPivotMode::SelectionBounds);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Use combined selection bounds as pivot"); }
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", mode_ == static_cast<int>(ImGuizmo::LOCAL)))
    {
        mode_ = static_cast<int>(ImGuizmo::LOCAL);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Local axes (Q)"); }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", mode_ == static_cast<int>(ImGuizmo::WORLD)))
    {
        mode_ = static_cast<int>(ImGuizmo::WORLD);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("World axes (Q)"); }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GizmoController::Draw(NextEngine& engine, const glm::vec2& viewportPos, const glm::vec2& viewportSize)
{
    Assets::Scene& scene = engine.GetScene();
    std::vector<uint32_t> selectedIds = BuildSelectionList(scene);
    if (selectedIds.empty())
    {
        ResetState();
        return;
    }

    uint32_t selectedId = scene.GetSelectedId();
    Assets::Node* activeNode = scene.GetNodeByInstanceId(selectedId);
    if (activeNode == nullptr)
    {
        selectedId = selectedIds.back();
        activeNode = scene.GetNodeByInstanceId(selectedId);
    }

    if (activeNode == nullptr || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        ResetState();
        return;
    }

    const bool multiSelection = selectedIds.size() > 1;
    if (multiSelection && !multiSelectionModeInitialized_)
    {
        pivotMode_ = static_cast<int>(EGizmoPivotMode::SelectionBounds);
        multiSelectionModeInitialized_ = true;
    }
    else if (!multiSelection)
    {
        multiSelectionModeInitialized_ = false;
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

    const bool useSelectionBounds = pivotMode_ == static_cast<int>(EGizmoPivotMode::SelectionBounds);
    glm::mat4 worldMatrix = BuildGizmoMatrix(scene, activeNode, useSelectionBounds);
    const ImGuizmo::MODE gizmoMode =
        useSelectionBounds ? ImGuizmo::WORLD : static_cast<ImGuizmo::MODE>(mode_);
    
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
    ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        static_cast<ImGuizmo::OPERATION>(operation_),
        gizmoMode,
        glm::value_ptr(worldMatrix));

    isUsing_ = ImGuizmo::IsUsing();
    isOver_ = ImGuizmo::IsOver();
    if (isUsing_ && !wasUsing_)
    {
        if (io.KeyShift)
        {
            auto duplicateCommand = std::make_unique<DuplicateNodesCommand>(scene, selectedIds);
            if (engine.ExecuteCommand(std::move(duplicateCommand)))
            {
                selectedIds = BuildSelectionList(scene);
                if (selectedIds.empty())
                {
                    ResetState();
                    return;
                }
                selectedId = scene.GetSelectedId();
                activeNode = scene.GetNodeByInstanceId(selectedId);
                if (activeNode == nullptr)
                {
                    selectedId = selectedIds.back();
                    activeNode = scene.GetNodeByInstanceId(selectedId);
                }
                worldMatrix = BuildGizmoMatrix(scene, activeNode, useSelectionBounds);
            }
        }

        dragActive_ = true;
        dragInstanceIds_ = BuildRootSelection(scene, selectedIds);
        dragStartWorldMatrices_.clear();
        dragStartSnapshots_.clear();
        dragStartWorldMatrices_.reserve(dragInstanceIds_.size());
        dragStartSnapshots_.reserve(dragInstanceIds_.size());
        for (uint32_t id : dragInstanceIds_)
        {
            Assets::Node* node = scene.GetNodeByInstanceId(id);
            if (node == nullptr)
            {
                continue;
            }

            dragStartWorldMatrices_.push_back(node->WorldTransform());
            dragStartSnapshots_.push_back(TransformSnapshot{node->Translation(), node->Rotation(), node->Scale()});
        }
        dragStartGizmoMatrix_ = worldMatrix;
    }

    if (isUsing_)
    {
        bool changed = false;
        const glm::mat4 deltaMatrix = worldMatrix * glm::inverse(dragStartGizmoMatrix_);
        const size_t count = std::min(dragInstanceIds_.size(), dragStartWorldMatrices_.size());
        for (size_t i = 0; i < count; ++i)
        {
            Assets::Node* node = scene.GetNodeByInstanceId(dragInstanceIds_[i]);
            if (node == nullptr)
            {
                continue;
            }

            const glm::mat4 targetWorld = deltaMatrix * dragStartWorldMatrices_[i];
            glm::mat4 parentWorld(1.0f);
            if (node->GetParent() != nullptr)
            {
                parentWorld = node->GetParent()->WorldTransform();
            }

            const glm::mat4 localMatrix = glm::inverse(parentWorld) * targetWorld;
            glm::vec3 scale{};
            glm::quat rotation{};
            glm::vec3 translation{};
            glm::vec3 skew{};
            glm::vec4 perspective{};
            if (!glm::decompose(localMatrix, scale, rotation, translation, skew, perspective))
            {
                continue;
            }

            node->SetTranslation(translation);
            node->SetRotation(rotation);
            node->SetScale(scale);
            node->RecalcTransform(true);
            changed = true;
        }

        if (changed)
        {
            scene.MarkDirty();
        }
    }
    else if (wasUsing_)
    {
        if (dragActive_ && !dragStartSnapshots_.empty())
        {
            std::vector<uint32_t> validIds;
            std::vector<TransformSnapshot> beforeSnapshots;
            std::vector<TransformSnapshot> afterSnapshots;
            const size_t count = std::min(dragInstanceIds_.size(), dragStartSnapshots_.size());
            validIds.reserve(count);
            beforeSnapshots.reserve(count);
            afterSnapshots.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                Assets::Node* node = scene.GetNodeByInstanceId(dragInstanceIds_[i]);
                if (node == nullptr)
                {
                    continue;
                }

                validIds.push_back(dragInstanceIds_[i]);
                beforeSnapshots.push_back(dragStartSnapshots_[i]);
                afterSnapshots.push_back({node->Translation(), node->Rotation(), node->Scale()});
            }

            if (!validIds.empty())
            {
                if (TransformNodesCommand::IsDifferent(beforeSnapshots, afterSnapshots))
                {
                    auto command = std::make_unique<TransformNodesCommand>(
                        scene, validIds, beforeSnapshots, afterSnapshots);
                    engine.ExecuteCommand(std::move(command));
                }
            }
        }
        dragActive_ = false;
        dragInstanceIds_.clear();
        dragStartWorldMatrices_.clear();
        dragStartSnapshots_.clear();
        dragStartGizmoMatrix_ = glm::mat4(1.0f);
    }

    wasUsing_ = isUsing_;

    io.WantCaptureMouse = io.WantCaptureMouse || isOver_ || isUsing_;
}
