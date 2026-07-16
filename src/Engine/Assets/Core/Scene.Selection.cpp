#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <algorithm>

namespace Assets
{
    namespace
    {
        constexpr float kMinimumFocusRadius = 0.001f;

        bool IsValidSelectionId(const Scene& scene, uint32_t id)
        {
            const uint32_t resolvedId = scene.ResolveEditableNodeId(id);
            return resolvedId != SceneSelectionState::invalidNodeId && scene.GetNodeSharedByInstanceId(resolvedId) != nullptr;
        }

        const std::vector<uint32_t>& ResolveEffectiveSelection(const Scene& scene, std::vector<uint32_t>& fallback)
        {
            const auto& ids = scene.GetSelectedIds();
            if (!ids.empty())
            {
                return ids;
            }

            const uint32_t selectedId = scene.GetSelectedId();
            if (selectedId != SceneSelectionState::invalidNodeId)
            {
                fallback.push_back(selectedId);
            }
            return fallback;
        }
    } // namespace

    void Scene::SetSelectedId(uint32_t id) const
    {
        selectionState_.SetSingle(ResolveEditableNodeId(id));
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::SetSelection(const std::vector<uint32_t>& ids) const
    {
        std::vector<uint32_t> validIds;
        validIds.reserve(ids.size());

        for (uint32_t id : ids)
        {
            const uint32_t resolvedId = ResolveEditableNodeId(id);
            if (!IsValidSelectionId(*this, resolvedId))
            {
                continue;
            }

            if (std::find(validIds.begin(), validIds.end(), resolvedId) == validIds.end())
            {
                validIds.push_back(resolvedId);
            }
        }

        selectionState_.SetMany(validIds);
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::ClearSelection() const
    {
        selectionState_.Clear();
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::AddToSelection(uint32_t id) const
    {
        const uint32_t resolvedId = ResolveEditableNodeId(id);
        if (!IsValidSelectionId(*this, resolvedId))
        {
            return;
        }

        selectionState_.Add(resolvedId);
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::RemoveFromSelection(uint32_t id) const
    {
        selectionState_.Remove(ResolveEditableNodeId(id));
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::ToggleSelection(uint32_t id) const
    {
        const uint32_t resolvedId = ResolveEditableNodeId(id);
        if (!IsValidSelectionId(*this, resolvedId))
        {
            return;
        }

        if (selectionState_.IsSelected(resolvedId))
        {
            RemoveFromSelection(resolvedId);
            return;
        }

        AddToSelection(resolvedId);
    }

    bool Scene::IsSelected(uint32_t id) const
    {
        return selectionState_.IsSelected(ResolveEditableNodeId(id));
    }

    uint32_t Scene::ResolveEditableNodeId(uint32_t id) const
    {
        if (id == SceneSelectionState::invalidNodeId)
        {
            return id;
        }

        const auto node = GetNodeSharedByInstanceId(id);
        if (node && node->IsSceneReferenceInternal())
        {
            return node->GetSceneReferenceOwnerProxyId();
        }
        return id;
    }

    void Scene::SetHoveredId(uint32_t id) const
    {
        const uint32_t resolvedId = ResolveEditableNodeId(id);
        if (!IsValidSelectionId(*this, resolvedId))
        {
            if (hoveredId_ != SceneSelectionState::invalidNodeId)
            {
                hoveredId_ = SceneSelectionState::invalidNodeId;
                const_cast<Scene*>(this)->MarkSelectionDirty();
            }
            return;
        }
        if (hoveredId_ != resolvedId)
        {
            hoveredId_ = resolvedId;
            const_cast<Scene*>(this)->MarkSelectionDirty();
        }
    }

    void Scene::ClearHoveredId() const
    {
        if (hoveredId_ != SceneSelectionState::invalidNodeId)
        {
            hoveredId_ = SceneSelectionState::invalidNodeId;
            const_cast<Scene*>(this)->MarkSelectionDirty();
        }
    }

    bool Scene::IsLocked(uint32_t id) const
    {
        return lockedIds_.find(ResolveEditableNodeId(id)) != lockedIds_.end();
    }

    void Scene::SetLocked(uint32_t id, bool locked) const
    {
        const uint32_t resolvedId = ResolveEditableNodeId(id);
        if (!IsValidSelectionId(*this, resolvedId))
        {
            return;
        }

        if (locked)
        {
            lockedIds_.insert(resolvedId);
            return;
        }

        lockedIds_.erase(resolvedId);
    }

    void Scene::ToggleLocked(uint32_t id) const
    {
        const uint32_t resolvedId = ResolveEditableNodeId(id);
        if (!IsValidSelectionId(*this, resolvedId))
        {
            return;
        }

        if (IsLocked(resolvedId))
        {
            lockedIds_.erase(resolvedId);
            return;
        }
        lockedIds_.insert(resolvedId);
    }

    bool Scene::GetSelectedNodeBounds(glm::vec3& center, float& radius) const
    {
        std::vector<uint32_t> fallbackSelection;
        const auto& ids = ResolveEffectiveSelection(*this, fallbackSelection);

        if (ids.empty())
        {
            return false;
        }

        glm::vec3 minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
        glm::vec3 maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        bool found = false;

        for (uint32_t id : ids)
        {
            glm::vec3 nodeCenter;
            float nodeRadius = 0.0f;
            if (!GetNodeBounds(id, nodeCenter, nodeRadius))
            {
                continue;
            }

            const glm::vec3 extent(nodeRadius);
            minBounds = glm::min(minBounds, nodeCenter - extent);
            maxBounds = glm::max(maxBounds, nodeCenter + extent);
            found = true;
        }

        if (!found)
        {
            return false;
        }

        center = (minBounds + maxBounds) * 0.5f;
        radius = glm::length(maxBounds - minBounds) * 0.5f;
        radius = std::max(radius, kMinimumFocusRadius);
        return true;
    }
} // namespace Assets
