#include "Engine/Assets/Core/Scene.hpp"

#include <algorithm>

namespace Assets
{
    namespace
    {
        constexpr float kMinimumFocusRadius = 0.001f;

        bool IsValidSelectionId(const Scene& scene, uint32_t id)
        {
            return id != SceneSelectionState::invalidNodeId && scene.GetNodeSharedByInstanceId(id) != nullptr;
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
        selectionState_.SetSingle(id);
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::SetSelection(const std::vector<uint32_t>& ids) const
    {
        std::vector<uint32_t> validIds;
        validIds.reserve(ids.size());

        for (uint32_t id : ids)
        {
            if (!IsValidSelectionId(*this, id))
            {
                continue;
            }

            validIds.push_back(id);
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
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        selectionState_.Add(id);
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::RemoveFromSelection(uint32_t id) const
    {
        selectionState_.Remove(id);
        const_cast<Scene*>(this)->MarkSelectionDirty();
    }

    void Scene::ToggleSelection(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (selectionState_.IsSelected(id))
        {
            RemoveFromSelection(id);
            return;
        }

        AddToSelection(id);
    }

    bool Scene::IsSelected(uint32_t id) const
    {
        return selectionState_.IsSelected(id);
    }

    void Scene::SetHoveredId(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            if (hoveredId_ != SceneSelectionState::invalidNodeId)
            {
                hoveredId_ = SceneSelectionState::invalidNodeId;
                const_cast<Scene*>(this)->MarkSelectionDirty();
            }
            return;
        }
        if (hoveredId_ != id)
        {
            hoveredId_ = id;
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
        return lockedIds_.find(id) != lockedIds_.end();
    }

    void Scene::SetLocked(uint32_t id, bool locked) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (locked)
        {
            lockedIds_.insert(id);
            return;
        }

        lockedIds_.erase(id);
    }

    void Scene::ToggleLocked(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (IsLocked(id))
        {
            lockedIds_.erase(id);
            return;
        }
        lockedIds_.insert(id);
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
